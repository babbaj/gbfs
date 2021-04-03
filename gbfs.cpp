#include <cstdio>
#include <cstddef>
#include <cassert>
#include <ctime>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <sys/types.h>

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <span>

#define FUSE_USE_VERSION 31

#include <fuse.h>

#include <sqlite3.h>

struct Options {
	const char *db_path;
	bool show_help;
};

struct File {
    std::string path; // full path
    int modifiedTime;
    int flags;
    int size;
};

struct Directory {
    std::string name; // short name
    std::map<std::string, Directory> directories;
    std::map<std::string, File> files; // short name to file
};

bool operator<(const File& a, const File& b) {
    return a.path < b.path;
}

sqlite3 *db = nullptr;

Directory rootDir;

#define OPTION(t, p)                           \
    { t, offsetof(Options, p), 1 }
static const struct fuse_opt option_spec[] = {
	OPTION("--db=%s", db_path),
	OPTION("-h", show_help),
	OPTION("--help", show_help),
	FUSE_OPT_END
};
#undef OPTION


static void initDatabase() {
    const int rc = sqlite3_open_v2("/home/babbaj/.gb.db", &db, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);

        exit(1);
    } else {
        puts("opened db");
    }
}

void closeDatabase() {
    if (db != nullptr) {
        sqlite3_close(db);
    }
}

void checkErr(int err) {
    if (err != SQLITE_OK) {
        fprintf(stderr, "sqlite error: %d, %s\n", err, sqlite3_errmsg(db));
        exit(1);
    }
}

std::vector<File> queryFullDirectory(const std::string& dir) {
    sqlite3_stmt* stmt;
    //int err = sqlite3_prepare_v2(db, "SELECT path, fs_modified, permissions FROM files WHERE path GLOB ? GROUP BY path;", -1, &stmt, nullptr);
    int err = sqlite3_prepare_v2(db, "SELECT path, fs_modified, permissions, size FROM files INNER JOIN sizes USING (hash) WHERE path GLOB ? GROUP BY path;", -1, &stmt, nullptr);
    checkErr(err);
    const std::string arg = (dir + "*");
    err = sqlite3_bind_text(stmt, 1, arg.c_str(), arg.size(), nullptr);
    checkErr(err);

    std::vector<File> files;
    while (true) {
        int res = sqlite3_step(stmt); // exec
        if (res == SQLITE_ROW) {
            auto* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const int modified = sqlite3_column_int(stmt, 1);
            const int perms = sqlite3_column_int(stmt, 2);
            const int size = sqlite3_column_int(stmt, 3);

            files.push_back(File {
                .path =  path,
                .modifiedTime = modified,
                .flags = perms,
                .size = size
            });
        } else {
            break;
        }
    }

    return files;
}

std::vector<std::string> split0(std::string_view str, std::string_view delim) {
    std::vector<std::string> out;
    size_t pos = 0;
    std::string token;
    std::string s = std::string{str};
    while ((pos = s.find(delim)) != std::string::npos) {
        token = s.substr(0, pos);
        out.push_back(token);
        s.erase(0, pos + delim.length());
    }
    out.push_back(s);
    return out;
}

Directory parseDirectoryStructure(std::span<const File> files) {
    Directory root{"/"};
    for (const auto& f : files) {
        Directory* dir = &root;
        auto split = split0(f.path, "/");
        for (int i = 1; i < split.size(); i++) { // start at 1 because first element will be empty
            const auto& element = split[i];
            // the last element is the file
            if (i == split.size() - 1) {
                auto pos = f.path.find_last_of('/');
                dir->files.emplace(f.path.substr(pos + 1), f);
            } else {
                auto dirIt = dir->directories.find(element);
                if (dirIt != dir->directories.end()) {
                    dir = &dirIt->second;
                } else {
                    Directory& newDir = dir->directories.emplace(element, Directory{element}).first->second;
                    //std::cout << "element = " << element << '\n';
                    dir = &newDir;
                }
            }
        }
        //std::cout << f.path << '\n';
    }

    return root;
}

template<int err, typename... Args>
static int unimplemented(Args...) {
    return -err;
}

static void* gbfs_init(fuse_conn_info *conn, fuse_config *cfg) {
    cfg->kernel_cache = 0;
    initDatabase();
    rootDir = parseDirectoryStructure(queryFullDirectory("/"));

    return nullptr;
}

// this function is probably not necessary
int gbfs_getattr(const char* path, struct stat* st, fuse_file_info *) {
    //std::cout << "gbfs_getattr: " << path << '\n';

    st->st_uid = 1000;
    st->st_gid = 100;

    /*if (strcmp( path, "/" ) == 0) {
		st->st_mode = S_IFDIR | 0755;
		st->st_nlink = 2;
	} else {
        // not actually a real file but don't want to confuse software
		st->st_mode = S_IFREG | 0644;
		st->st_nlink = 1;
		st->st_size = 666;
	}*/

    std::string_view view{path};
    if (view.size() > 1 && view.ends_with('/')) {
        view = view.substr(0, view.size() - 1);
    }
    auto split = split0(view, "/");
    Directory* dir = &rootDir;
    for (int i = 1; i < split.size(); i++) { // skip root
        const auto& name = split[i];
        if (name.empty()) break;
        auto it = dir->directories.find(name);
        if (it != dir->directories.end()) {
            dir = &it->second;
        } else {
            auto fileIt = dir->files.find(name);
            if (fileIt != dir->files.end()) {
                // it is a file!
                const File& file = fileIt->second;

                st->st_mode = S_IFREG | file.flags;
                st->st_nlink = 1;
                st->st_size = file.size;
                st->st_atime = file.modifiedTime;
                st->st_mtime = file.modifiedTime;
                return 0;
            } else {
                return -ENOENT;
            }
        }
    }

    // it is a directory
    st->st_mode = S_IFDIR | 0755;
    st->st_nlink = 2;
    st->st_atime = time(nullptr);
    st->st_mtime = time(nullptr);

    return 0;
}

static int gbfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, [[maybe_unused]] off_t offset, [[maybe_unused]] fuse_file_info *fi, fuse_readdir_flags) {
    //std::cout << "gbfs_readdir: " << path << '\n';

    filler(buffer, ".", nullptr, 0, FUSE_FILL_DIR_PLUS);
    filler(buffer, "..", nullptr, 0, FUSE_FILL_DIR_PLUS);

    std::string_view view{path};
    if (view.size() > 1 && view.ends_with('/')) {
        view = view.substr(0, view.size() - 1);
    }
    auto split = split0(view, "/");
    Directory* dir = &rootDir;
    for (int i = 1; i < split.size(); i++) { // skip root
        const auto& name = split[i];
        if (name.empty()) break;
        auto it = dir->directories.find(name);
        if (it != dir->directories.end()) {
            dir = &it->second;
        } else {
            return -ENOENT;
        }
    }

    //std::cout << "dir = " << dir->name << '\n';
    for (const auto& [name, dir] : dir->directories) {
        filler(buffer, name.c_str(), nullptr, 0, FUSE_FILL_DIR_PLUS);
    }
    for (const auto& [name, file] : dir->files) {
        filler(buffer, name.c_str(), nullptr, 0, FUSE_FILL_DIR_PLUS);
    }

    return 0;
}

static const fuse_operations gbfs_operations = {
    .getattr = &gbfs_getattr,
    //.read    = &unimplemented<ENOTSUP, const char *, char*, size_t, off_t, fuse_file_info*>,
    .readdir = &gbfs_readdir,
    .init    = &gbfs_init
};

static void show_help(const char *progname)
{
	printf("usage: %s [options] <mountpoint>\n\n", progname);
	printf("gbfs options:\n"
	       "    --db=<s>          Path to the \"db\" file\n"
	       "                        (default: \"~/.gb.db\")\n"
	       "\n");
}

void printDirectoryTree(const Directory& dir) {
    std::cout << dir.name << ":\n";
    for (const auto& [name, f] : dir.files) {
        std::cout << f.path << '\n';
    }
    for (const auto& pair : dir.directories) {
        printDirectoryTree(pair.second);
    }
}

int main(int argc, char** argv) {
    printf("%s\n", sqlite3_libversion());
    /*initDatabase();
    const std::vector files = queryFullDirectory("/home/babbaj/Pictures");
    auto tree = parseDirectoryStructure(files);

    printDirectoryTree(tree);

    closeDatabase();
    return 0;*/

    fuse_args args = FUSE_ARGS_INIT(argc, argv);

    Options options{};
    if (fuse_opt_parse(&args, &options, option_spec, nullptr) == -1) {
        return 1;
    }

    if (options.show_help) {
		show_help(argv[0]);
		assert(fuse_opt_add_arg(&args, "--help") == 0);
		args.argv[0][0] = '\0';
	}

    const int ret = fuse_main(args.argc, args.argv, &gbfs_operations, nullptr);

    fuse_opt_free_args(&args);
    closeDatabase();
    return ret;
}