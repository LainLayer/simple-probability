#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sendfile.h>

#define NOB_IMPLEMENTATION
#include "nob.h"

#include "extern/raylib/src/raylib.h"

#define RAYLIB_FLAGS "-w",\
                     "-ggdb", "-Og", \
                     "-D_GLFW_X11", \
                     "-DPLATFORM_DESKTOP",\
                     "-I./extern/raylib/src"

#define RAYLIB_SOURCE_PATH "extern/raylib/src/"

static const char *raylib_filenames[] = {
    "raudio",
    "rmodels",
    "rtextures",
    "rcore",
    "rshapes",
    "utils",
    "rglfw",
    "rtext",
};

#define RAYLIB_FILE_COUNT (sizeof(raylib_filenames)/sizeof(raylib_filenames[0]))

Nob_Proc build_single_raylib_object_file(const char *name) {
    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd, "cc", RAYLIB_FLAGS, "-c", "-o",
        nob_temp_sprintf("build/%s.o", name),
        nob_temp_sprintf(RAYLIB_SOURCE_PATH "%s.c", name),
    );

    return nob_cmd_run_async(cmd);
}

bool build_raylib(void) {

    char *input_paths[RAYLIB_FILE_COUNT+1] = {0};

    input_paths[0] = RAYLIB_SOURCE_PATH "config.h"; // Rebuild on config changes too

    for(size_t i = 1; i < RAYLIB_FILE_COUNT+1; i++)
        input_paths[i] = nob_temp_sprintf(RAYLIB_SOURCE_PATH "%s.c", raylib_filenames[i-1]);

    if(!nob_needs_rebuild("build/libraylib.a", (const char **)input_paths, RAYLIB_FILE_COUNT+1))
        return true;

    Nob_Proc async_pool[RAYLIB_FILE_COUNT] = {0};

    for(size_t i = 0; i < RAYLIB_FILE_COUNT; i++)
            async_pool[i] = build_single_raylib_object_file(raylib_filenames[i]);

    Nob_Procs procs = (Nob_Procs){
        .items    = &async_pool[0],
        .count    = RAYLIB_FILE_COUNT,
        .capacity = RAYLIB_FILE_COUNT
    };

    if(!nob_procs_wait(procs)) return false;

    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd, "ar", "-rcs", "build/libraylib.a");
    for(size_t i = 0; i < RAYLIB_FILE_COUNT; i++)
        nob_cmd_append(&cmd, nob_temp_sprintf("build/%s.o", raylib_filenames[i]));

    if(!nob_cmd_run_sync(cmd)) return false;

    return true;

}

bool build_assets(void) {

    Nob_File_Paths asset_files = {0};

    nob_read_entire_dir("assets", &asset_files);

    Nob_File_Paths asset_paths = {0};

    for(size_t i = 2; i < asset_files.count; i++)
        nob_da_append(&asset_paths, nob_temp_sprintf("assets/%s", asset_files.items[i]));

    if(!nob_needs_rebuild("./build/asset_package.qop", asset_paths.items, asset_paths.count)) return true;

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "./tools/qopconv", "assets", "./build/asset_package.qop");

    if(!nob_cmd_run_sync(cmd)) return false;

    return true;

}

bool build_microui(void) {

    const char *microui_c = "./extern/microui/src/microui.c";
    const char *microui_h = "./extern/microui/src/microui.h";

    const char *microui_files[] = { microui_c, microui_h };

    if(!nob_needs_rebuild("./build/microui.o", microui_files, 2)) return true;

    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd,
        "cc", "-c",
        "-o", "./build/microui.o",
        microui_c,
        "-std=c89", "-ggdb", "-Og", "-w",
        "-I./extern/microui/src/"
    );

    if(!nob_cmd_run_sync(cmd)) return false;

    return true;
}

bool build_microui_raylib(void) {

    const char *murl_c = "./extern/microui-raylib/src/murl.c";
    const char *murl_h = "./extern/microui-raylib/src/murl.h";

    const char *murl_files[] = { murl_c, murl_h };

    if(!nob_needs_rebuild("./build/murl.o", murl_files, 2)) return true;

    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd,
        "cc", "-c",
        "-o", "./build/murl.o",
        murl_c,
        "-std=c99", "-ggdb", "-Og", "-w",
        "-I./extern/microui-raylib/src/",
        "-I./extern/microui/src/"
    );

    if(!nob_cmd_run_sync(cmd)) return false;

    return true;
}

bool build_qopconv(void) {

    const char *qopconv_source = "./extern/qop/qopconv.c";

    const char *qop_files[] = { qopconv_source, "./extern/qop/qop.h" };

    if(!nob_needs_rebuild("./tools/qopconv", qop_files, 2)) return true;

    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd, "cc", "-o", "./tools/qopconv", qopconv_source, "-ggdb", "-Og", "-w", "-I./extern/qop");

    if(!nob_cmd_run_sync(cmd)) return false;

    return true;
}

bool append_asset_package_to_end_of_executable(void) {

    int asset_package_fileno = open("./build/asset_package.qop", O_RDONLY);
    if(asset_package_fileno < 0) return false;
    struct stat asset_package_stats;
    fstat(asset_package_fileno, &asset_package_stats);

    int executable_fileno = open("./dice", O_WRONLY, asset_package_stats.st_mode);
    lseek(executable_fileno, 0, SEEK_END);
    if(executable_fileno < 0) return false;

    nob_log(NOB_INFO, "Sending %d bytes from file(%d) to file(%d)", asset_package_stats.st_size, asset_package_fileno, executable_fileno);

    // man sendfile: "may  write  fewer bytes than requested; the caller should be prepared to retry the call if there were unsent bytes."
    // I do not trust this function.
    ssize_t total_sent = 0;
    do {
        nob_log(NOB_INFO, "Iteration");
        ssize_t result = sendfile(executable_fileno,
                                  asset_package_fileno, NULL, (size_t)asset_package_stats.st_size);
        if(result < 0) {
            nob_log(NOB_ERROR, "Failed to pack assets into executable in `sendfile`: %s", strerror(errno));
            close(asset_package_fileno);
            close(executable_fileno);
            return false;
        } else {
            total_sent += result;
        }
    } while(total_sent < asset_package_stats.st_size);

    close(asset_package_fileno);
    close(executable_fileno);

    nob_log(NOB_INFO, "Sent total of %d bytes", total_sent);

    return true;
}

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    if(!nob_mkdir_if_not_exists("build")) {
        nob_log(NOB_ERROR, "Failed to create build directory");
        return 1;
    }

    if(!nob_mkdir_if_not_exists("tools")) {
        nob_log(NOB_ERROR, "Failed to create tools directory");
        return 1;
    }

    if(!build_raylib()) {
        nob_log(NOB_ERROR, "Failed to build raylib");
        return 1;
    }

    if(!build_qopconv()) {
        nob_log(NOB_ERROR, "Failed to build qopconv");
        return 1;
    }

    if(!build_assets()) {
        nob_log(NOB_ERROR, "Failed to build image assets into code");
        return 1;
    }

    if(!build_microui()) {
        nob_log(NOB_ERROR, "Failed to build microui");
        return 1;
    }

    if(!build_microui_raylib()) {
        nob_log(NOB_ERROR, "Failed to build microui-raylib");
        return 1;
    }

    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd,
        "cc", "-Wall", "-Wextra",
        "-o", "dice", "dice.c",
        "-L./build", "-l:libraylib.a", "-I./" RAYLIB_SOURCE_PATH, "-lm",
        "./build/microui.o", "-I./extern/microui/src/",
        "./build/murl.o", "-I./extern/microui-raylib/src/",
        "-I./extern/qop/"
    );

    if(!nob_cmd_run_sync(cmd)) return 1;

    if(!append_asset_package_to_end_of_executable()) return 1;

    return 0;
}
