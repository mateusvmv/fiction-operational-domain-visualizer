#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum BuildType BuildType;
enum BuildType
{
    BuildRelease,
    BuildDebug
};

typedef enum RunType RunType;
enum RunType
{
    RunNormal,
    RunGDB,
    RunGF
};

const char *target_folder(BuildType build_type)
{
    if (build_type == BuildDebug) return "target/debug";
    return "target/release";
}

int run_with_tee(const char *command, const char *failed_log_path)
{
    char full_cmd[1024];

    snprintf(full_cmd, sizeof(full_cmd), "%s 2>&1", command);

    // Ensure the target directory exists so we can write the log directly
    (void)system("mkdir -p target");

    FILE *fp = popen(full_cmd, "r");
    if (!fp)
    {
        perror("popen failed");
        return -1;
    }

    FILE *fail_file = fopen(failed_log_path, "w");
    if (!fail_file)
    {
        perror("Failed to open log file");
        pclose(fp);
        return -1;
    }

    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), fp) != NULL)
    {
        fputs(buffer, stdout);    // Print live to console
        fputs(buffer, fail_file); // Save to log file directly
    }

    fclose(fail_file);

    int status = pclose(fp);
    int exit_code = -1;
    if (WIFEXITED(status))
    {
        exit_code = WEXITSTATUS(status);
    }

    if (exit_code != 0)
    {
        fprintf(stderr, "\n==> Compilation failed! Error log dumped to %s\n", failed_log_path);
    }
    else
    {
        // Clean up the log file on success so it only exists when a build fails
        remove(failed_log_path);
    }

    return exit_code;
}

int compile(BuildType build_type)
{
    const char *build_dir = target_folder(build_type);
    const char *b_type_str = (build_type == BuildDebug) ? "Debug" : "Release";

    char command[512];

    // Configure CMake (standard system call is fine for configuration)
    printf("==> Configuring CMake (%s mode)...\n", b_type_str);
    snprintf(command, sizeof(command), "cmake -S . -B %s -G Ninja -DCMAKE_BUILD_TYPE=%s", build_dir, b_type_str);
    int ret = system(command);
    if (ret != 0)
    {
        return ret;
    }

    // Build project with live printing and failure logging
    printf("==> Building project...\n");
    snprintf(command, sizeof(command), "cmake --build %s --parallel", build_dir);
    ret = run_with_tee(command, "target/failed_compilation.txt");
    return ret;
}

int run(BuildType build_type, RunType run_type)
{
    const char *build_dir = target_folder(build_type);
    char exec_path[512];
    snprintf(exec_path, sizeof(exec_path), "%s/tester", build_dir);

    char command[1024];
    switch (run_type) {
        case RunNormal:
            printf("==> Running %s...\n", exec_path);
            snprintf(command, sizeof(command), "%s", exec_path);
            break;
        case RunGDB:
            printf("==> Launching GDB on %s...\n", exec_path);
            snprintf(command, sizeof(command), "gdb --args %s", exec_path);
            break;
        case RunGF:
            printf("==> Launching GF debugger on %s...\n", exec_path);
            snprintf(command, sizeof(command), "gf2 %s", exec_path);
            break;
    }

    return system(command);
}

int main(int argc, char *argv[])
{
    BuildType build_type = BuildRelease;
    RunType run_type = RunNormal;
    int should_run = 0;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "debug") == 0) {
            build_type = BuildDebug;
        } else if (strcmp(argv[i], "release") == 0) {
            build_type = BuildRelease;
        } else if (strcmp(argv[i], "run") == 0) {
            should_run = 1;
            run_type = RunNormal;
        } else if (strcmp(argv[i], "gdb") == 0) {
            should_run = 1;
            run_type = RunGDB;
            build_type = BuildDebug; // Automatically ensure debug mode for debugging
        } else if (strcmp(argv[i], "gf") == 0) {
            should_run = 1;
            run_type = RunGF;
            build_type = BuildDebug; // Automatically ensure debug mode for debugging
        } else {
            fprintf(stderr, "Error: Unknown argument '%s'.\n", argv[i]);
            fprintf(stderr, "Usage: ./build [release|debug] [run|gdb|gf]\n");
            return 1;
        }
    }

    // Compile step
    int ret = compile(build_type);
    if (ret != 0) {
        return ret;
    }

    // Run step (if requested)
    if (should_run) {
        ret = run(build_type, run_type);
    }

    return ret;
}
