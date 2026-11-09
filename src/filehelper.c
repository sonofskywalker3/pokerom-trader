#include "filehelper.h"
#ifdef _WIN32
#include <errno.h>
#include <Windows.h>
#include <tchar.h>
#include <stdint.h>
#include <time.h>
#else
#include <unistd.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

#ifdef __APPLE__
#define USR_DATA_DIR "/Library/Application Support/PokeromTrader"
#elif _WIN32
#define USR_DATA_DIR "\\Documents\\PokeromTrader"
#else
#define USR_DATA_DIR "/.pokeromtrader"
#endif

// The user's home folder, or "." if the variable is missing (strcpy of a NULL
// getenv() result would crash).
static const char *home_dir(void)
{
#ifdef _WIN32
    const char *h = getenv("USERPROFILE");
#else
    const char *h = getenv("HOME");
#endif
    return h ? h : ".";
}

// True if `name` ends in exactly ".sav" (not .sav_bak / .sav.tmp / .sav.old,
// the app's own backup and side files).
static bool has_sav_suffix(const char *name)
{
    size_t n = strlen(name);
#ifdef _WIN32
    return n >= 4 && _stricmp(name + n - 4, ".sav") == 0;
#else
    return n >= 4 && strcmp(name + n - 4, ".sav") == 0;
#endif
}

char *resolved_path = NULL;
char *absolute_path = NULL;
#ifdef _WIN32
int get_save_files(struct save_file_data *save_data)
{
    // Get .sav files from save folder
    char save_dir[MAX_FILE_PATH_CHAR];
    strcpy(save_dir, (char *)save_data->save_dir);

    WIN32_FIND_DATA find_file_data;
    HANDLE hFind;
    char saves[MAX_FILE_PATH_CHAR];
    int pattern_len = snprintf(saves, sizeof(saves), "%s\\*.sav", save_dir);

    hFind = (pattern_len > 0 && pattern_len < (int)sizeof(saves)) ? FindFirstFile(saves, &find_file_data) : INVALID_HANDLE_VALUE;

    if (hFind == INVALID_HANDLE_VALUE)
    {
        printf("FindFirstFile failed (%d)\n", GetLastError());
        save_data->num_saves = 0;
        // return 1;
    }
    else
    {
        int num_saves = 0;
        do
        {
            // FindFirstFile's *.sav also matches 8.3 short names, which pulls
            // in .sav_bak backups — require the real name to end in .sav.
            size_t name_len = strlen(find_file_data.cFileName);
            if (name_len < 4 || _stricmp(find_file_data.cFileName + name_len - 4, ".sav") != 0)
                continue;
            // The path table holds MAX_FILE_PATH_COUNT entries; ignore the rest.
            if (num_saves >= MAX_FILE_PATH_COUNT)
                break;
            // Combine the base path and file name (skip paths too long to hold)
            char full_path[MAX_FILE_PATH_CHAR];
            int len = snprintf(full_path, sizeof(full_path), "%s/%s", save_dir, find_file_data.cFileName);
            if (len < 0 || len >= (int)sizeof(full_path))
                continue;
            strcpy(save_data->saves_file_path[num_saves], full_path);
            num_saves++;
        } while (FindNextFile(hFind, &find_file_data) != 0);
        FindClose(hFind);
        save_data->num_saves = num_saves;
    }

    return 0;
}
int write_key_to_config(const char *key, const char *value)
{
    char config_path[MAX_FILE_PATH_CHAR];
    snprintf(config_path, sizeof(config_path), "%s", home_dir());
    strcat(config_path, USR_DATA_DIR);
    strcat(config_path, "\\config.ini");

    LPCSTR ini = config_path;

    WINBOOL error = WritePrivateProfileStringA("app", key, value, ini);
    if (error == FILE_OP_FAILURE)
        error_handler(error, "Error writing to config.ini");
    return error;
}
void create_default_config(void)
{
    char config_path[MAX_PATH];

    // Get the current working directory
    snprintf(config_path, sizeof(config_path), "%s", home_dir());
    strcat(config_path, USR_DATA_DIR);

    // Create the saves folder
    char saves_dir[MAX_PATH];
    strcpy(saves_dir, config_path);
    strcat(saves_dir, "\\saves");

    // Create the logs folder
    char logs_dir[MAX_PATH];
    strcpy(logs_dir, config_path);
    strcat(logs_dir, "\\logs");

    // Create directories
    if (!CreateDirectory(config_path, NULL))
    {
        if (GetLastError() != ERROR_ALREADY_EXISTS)
        {
            printf("Error creating directory: %d\n", GetLastError());
            printf("Path: %s\n", config_path);
            return; // never exit the app over this; callers carry on without a config
        }
    }

    if (!CreateDirectory(saves_dir, NULL))
    {
        if (GetLastError() != ERROR_ALREADY_EXISTS)
        {
            printf("Error creating saves directory: %d\n", GetLastError());
            return; // never exit the app over this; callers carry on without a config
        }
    }

    if (!CreateDirectory(logs_dir, NULL))
    {
        if (GetLastError() != ERROR_ALREADY_EXISTS)
        {
            printf("Error creating logs directory: %d\n", GetLastError());
            return; // never exit the app over this; callers carry on without a config
        }
    }

    // Create config.ini file in cwd
    strcat(config_path, "\\config.ini");
    FILE *fp = fopen(config_path, "w+");

    if (fp == NULL)
    {
        printf("Error creating file: %d\n", GetLastError());
        return; // never exit the app over this; callers carry on without a config
    }

    fputs("# This is a generated file. Only modify values, not keys.\n", fp);
    fclose(fp);

    LPCSTR ini = config_path;
    WritePrivateProfileStringA("app", "SAVE_FILE_DIR", saves_dir, ini);
}
struct config_data read_key_from_config(void)
{
    char config_path[MAX_FILE_PATH_CHAR];
    snprintf(config_path, sizeof(config_path), "%s", home_dir());
    strcat(config_path, USR_DATA_DIR);
    strcat(config_path, "\\config.ini");

    // Create the folders and a default config if config.ini is missing. (This
    // used to check only for the folder, so a folder made by hand or left
    // behind without config.ini never got a save folder: "DIR_NOT_SET".)
    if (GetFileAttributes(config_path) == INVALID_FILE_ATTRIBUTES)
    {
        create_default_config();
    }

    LPCSTR ini = config_path;
    // Static storage: struct config_data.save_file_dir is a pointer, and the
    // caller reads it after this function returns. A local (stack) buffer here
    // would be a dangling pointer, which previously caused the saved folder to
    // be lost on every launch.
    static char save_file_str[MAX_FILE_PATH_CHAR];
    GetPrivateProfileString("app", "SAVE_FILE_DIR", "DIR_NOT_SET", save_file_str, MAX_FILE_PATH_CHAR, ini);

    return (struct config_data){
        .save_file_dir = save_file_str
    };
}
void write_to_log(const char *msg, const uint8_t message_type)
{
    FILE *fp;

    char date[20];
    char time_str[20];
    time_t now = time(NULL);
    struct tm t = {0};
    localtime_s(&t, &now);

    strftime(date, sizeof(date) - 1, "%Y-%m-%d", &t);
    strftime(time_str, sizeof(time_str) - 1, "%H:%M:%S", &t);

    const char *kind;
    switch (message_type)
    {
    case E_LOG_MESSAGE_TYPE_ERROR:
        kind = "error-log@";
        break;
    case E_LOG_MESSAGE_TYPE_INFO:
        kind = "info-log@";
        break;
    default:
        kind = "log@";
        break;
    }

    // Logging must never take the app down: callers log in the middle of a
    // save or trade, and exiting there would abandon the operation half-done.
    // If the log can't be opened, report to stderr and carry on.
    const char *home = getenv("USERPROFILE");
    char config_path[MAX_FILE_PATH_CHAR];
    int len = snprintf(config_path, sizeof(config_path), "%s%s\\logs\\%s%s.txt",
                       home ? home : ".", USR_DATA_DIR, kind, date);
    fp = (len > 0 && len < (int)sizeof(config_path)) ? fopen(config_path, "a+") : NULL;

    if (fp == NULL)
    {
        fprintf(stderr, "PokeromTrader: could not open log file (%lu): %s\n", (unsigned long)GetLastError(), msg);
        return;
    }

    fputs(time_str, fp);
    fputs(": ", fp);
    fputs(msg, fp);
    fputs("\n", fp);

    fclose(fp);
}
int delete_app_data(void)
{
    char config_path[MAX_FILE_PATH_CHAR];
    snprintf(config_path, sizeof(config_path), "%s", home_dir());
    strcat(config_path, USR_DATA_DIR);

    // Delete config.ini
    char config_file[MAX_FILE_PATH_CHAR];
    strcpy(config_file, config_path);
    strcat(config_file, "\\config.ini");
    remove(config_file);

    // Delete all .sav files in saves/
    char saves_dir[MAX_FILE_PATH_CHAR];
    strcpy(saves_dir, config_path);
    strcat(saves_dir, "\\saves");

    WIN32_FIND_DATA find_file_data;
    HANDLE hFind;
    char saves[MAX_FILE_PATH_CHAR];
    strcpy(saves, saves_dir);
    strcat(saves, "\\*.sav");

    hFind = FindFirstFile(saves, &find_file_data);

    if (hFind == INVALID_HANDLE_VALUE)
    {
        printf("Empty (saves) (%d)\n", GetLastError());
    }
    else
    {
        do
        {
            // hfind contains .sav extension
            // Saves only: keep the .sav_bak backups (*.sav also matches
            // their 8.3 short names) and other side files.
            if (!has_sav_suffix(find_file_data.cFileName))
            {
                continue;
            }

            // Combine the base path and file name
            char full_path[MAX_FILE_PATH_CHAR];
            sprintf(full_path, "%s/%s", saves_dir, find_file_data.cFileName);
            // Delete file
            remove(full_path);
            unlink(full_path);
        } while (FindNextFile(hFind, &find_file_data) != 0);
        FindClose(hFind);
    }

    // Delete saves/ directory
    int status = rmdir(saves_dir);
    if (status == -1)
    {
        printf("Error deleting saves directory: %d\n", GetLastError());
    }

    // Delete logs/ directory
    char logs_dir[MAX_FILE_PATH_CHAR];
    strcpy(logs_dir, config_path);
    strcat(logs_dir, "\\logs");

    char logs[MAX_FILE_PATH_CHAR];
    strcpy(logs, logs_dir);
    strcat(logs, "\\*.txt");

    hFind = FindFirstFileA(logs, &find_file_data);

    if (hFind == INVALID_HANDLE_VALUE)
    {
        printf("Empty (logs) (%d)\n", GetLastError());
    }
    else
    {
        do
        {
            // Combine the base path and file name
            char full_path[MAX_FILE_PATH_CHAR];
            sprintf(full_path, "%s/%s", logs_dir, find_file_data.cFileName);

            // Delete file
            remove(full_path);
            unlink(full_path);
        } while (FindNextFile(hFind, &find_file_data) != 0);
        FindClose(hFind);
    }

    // Delete logs/ directory
    status = rmdir(logs_dir);
    if (status == -1)
    {
        printf("Error deleting logs directory: %d\n", GetLastError());
    }

    // Delete PokeromTrader/ directory
    status = rmdir(config_path);
    if (status == -1)
    {
        printf("Error deleting PokeromTrader directory: %d\n", GetLastError());
    }

    return 0;
}

void init_settings_from_config(struct save_file_data *save_file_data)
{
    // Read and save the saves file directory from config.ini
    struct config_data config_data = read_key_from_config();

    char *save_file_dir = malloc(strlen(config_data.save_file_dir) + 1);
    if (save_file_dir != NULL)
    {
        strcpy(save_file_dir, config_data.save_file_dir);
        snprintf((char *)save_file_data->save_dir, sizeof(save_file_data->save_dir), "%s", save_file_dir);
    }
    else
    {
        strcpy((char *)save_file_data->save_dir, "DIR_NOT_SET");
    }

    free(save_file_dir);
}

#else

int get_save_files(struct save_file_data *save_data)
{
    DIR *dir;
    struct dirent *entry;
    char save_dir[MAX_FILE_PATH_CHAR];
    strcpy(save_dir, (char *)save_data->save_dir);
    int num_saves = 0;

    dir = opendir(save_dir);
    if (dir == NULL)
    {
        perror("Error opening directory: ");
        printf("Path: %s\n", save_dir);
        save_data->num_saves = 0;
        return 1;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        // Check if the entry is a regular file ending in .sav (not the app's
        // .sav_bak / .sav.tmp / .sav.old side files)
        size_t name_len = strlen(entry->d_name);
        if (num_saves >= MAX_FILE_PATH_COUNT)
        {
            break; // the path table holds MAX_FILE_PATH_COUNT entries
        }
        if (entry->d_type == DT_REG && name_len >= 4 && strcmp(entry->d_name + name_len - 4, ".sav") == 0)
        {
            // Combine the base path and file name
            char full_path[strlen(save_dir) + 1 + strlen(entry->d_name) + 1];
            sprintf(full_path, "%s/%s", save_dir, entry->d_name);

            // Get the absolute path
            absolute_path = realpath(full_path, NULL);
            if (absolute_path && strlen(absolute_path) < MAX_FILE_PATH_CHAR)
            {
                strcpy(save_data->saves_file_path[num_saves],absolute_path);
                num_saves++;
            }
            free(absolute_path);
            absolute_path = NULL;
        }
    }

    closedir(dir);
    save_data->num_saves = num_saves;
    return 0;
}

int write_key_to_config(const char *key, const char *value)
{
    // Get config.ini path
    char config_path[MAX_FILE_PATH_CHAR];
    snprintf(config_path, sizeof(config_path), "%s", home_dir());
    strcat(config_path, USR_DATA_DIR);
    strcat(config_path, "/config.ini");
    // Open the INI file for reading and writing.
    FILE *file = fopen(config_path, "r+");
    if (file == NULL)
    {
        perror("Error opening INI file");
        return FILE_OP_FAILURE;
    }

    // Create a temporary file to store the modified contents.
    FILE *temp_file = tmpfile();
    if (temp_file == NULL)
    {
        perror("Error creating temporary file");
        fclose(file);
        return FILE_OP_FAILURE;
    }

    char line[1024];
    int key_found = 0;

    // Read the INI file line by line.
    while (fgets(line, sizeof(line), file) != NULL)
    {
        // Check if the line starts with the desired key.
        if (strncmp(line, key, strlen(key)) == 0 && line[strlen(key)] == '=')
        {
            // Key found, update the value.
            fprintf(temp_file, "%s=%s\n", key, value);
            key_found = 1;
        }
        else
        {
            // Copy the line as-is to the temporary file.
            fprintf(temp_file, "%s", line);
        }
    }

    // If the key was not found, add it to the end of the file.
    if (!key_found)
    {
        fprintf(temp_file, "%s=%s\n", key, value);
    }

    // Close both files.
    fclose(file);
    rewind(temp_file);

    // Reopen the INI file for writing and copy the contents from the temporary file.
    file = fopen(config_path, "w");
    if (file == NULL)
    {
        perror("Error reopening INI file");
        fclose(temp_file);
        return FILE_OP_FAILURE;
    }

    while (fgets(line, sizeof(line), temp_file) != NULL)
    {
        fprintf(file, "%s", line);
    }

    // Close the temporary file.
    fclose(temp_file);

    // Close the INI file.
    fclose(file);

    return FILE_OP_SUCCESS;
}

void create_default_config(bool overwrite)
{
    FILE *fp;
    char cwd[MAX_FILE_PATH_CHAR];

    // Get current working dir
    snprintf(cwd, sizeof(cwd), "%s", home_dir());

    // create directory PokeromTrader
    const char *dir_path = USR_DATA_DIR;
    strcat(cwd, dir_path);

    // create saves folder
    char saves_dir[MAX_FILE_PATH_CHAR];
    strcpy(saves_dir, cwd);
    strcat(saves_dir, "/saves");

    // create logs folder
    char logs_dir[MAX_FILE_PATH_CHAR];
    strcpy(logs_dir, cwd);
    strcat(logs_dir, "/logs");

    (void)overwrite; // the folders are (re)created either way; mkdir ignores existing ones
    {

        int status = mkdir(cwd, 0777);
        if (status == -1)
        {
            printf("Error %d creating directory!\n", errno);
            if (errno != EEXIST)
                return; // never exit the app over this
        }

        status = mkdir(saves_dir, 0777);
        if (status == -1)
        {
            puts("Error creating saves directory");
            if (errno != EEXIST)
                return; // never exit the app over this
        }

        status = mkdir(logs_dir, 0777);
        if (status == -1)
        {
            puts("Error creating logs directory");
            if (errno != EEXIST)
                return; // never exit the app over this
        }
    }

    // create config.ini file in cwd
    strcat(cwd, "/config.ini");
    fp = fopen(cwd, "w+");

    if (fp == NULL)
    {
        printf("Error %d creating file!\n", errno);
        return; // never exit the app over this
    }

    fputs("# This is a generated file. Only modify values not keys.\n", fp);

    // Write default key values to new config.ini
    char default_key[MAX_FILE_PATH_CHAR];
    strcpy(default_key, "SAVE_FILE_DIR=");
    strcat(default_key, saves_dir);
    fputs(default_key, fp);

    fclose(fp);
}

char *read_key_from_config(const char *key)
{
    FILE *fp;
    char *line = NULL;
    size_t len = 0;
    char *value = NULL;

    char config_path[MAX_FILE_PATH_CHAR];
    snprintf(config_path, sizeof(config_path), "%s", home_dir());
    strcat(config_path, USR_DATA_DIR);
    strcat(config_path, "/config.ini");

    fp = fopen(config_path, "r");

    // If missing, then create
    if (fp == NULL)
    {
        create_default_config(false);

        // open after creation
        fp = fopen(config_path, "r");
        if (fp == NULL)
        {
            return NULL;
        }
    }

    while (getline(&line, &len, fp) != -1)
    {
        // if the line starts with a # or newline, skip it
        if (line[0] == '#' || line[0] == '\n')
        {
            continue;
        }

        // Split on the first '=' only: a folder name may contain '=', and
        // "KEY=" with nothing after it must not crash.
        char *eq = strchr(line, '=');
        if (eq == NULL)
        {
            continue;
        }
        *eq = 0;
        if (strcmp(line, key) == 0)
        {
            char *token = eq + 1;
            // remove newline from end of token
            token[strcspn(token, "\r\n")] = 0;
            value = malloc(strlen(token) + 1);
            if (value != NULL)
            {
                strcpy(value, token);
            }
            break;
        }
    }

    fclose(fp);
    if (line)
    {
        free(line);
    }
    return value;
}

int delete_app_data(void)
{
    char config_path[MAX_FILE_PATH_CHAR];
    snprintf(config_path, sizeof(config_path), "%s", home_dir());
    strcat(config_path, USR_DATA_DIR);

    DIR *dir;
    struct dirent *entry;

    // Delete config.ini
    char config_file[MAX_FILE_PATH_CHAR];
    strcpy(config_file, config_path);
    strcat(config_file, "/config.ini");
    remove(config_file);

    // Delete all .sav files in saves/
    char saves_dir[MAX_FILE_PATH_CHAR];
    strcpy(saves_dir, config_path);
    strcat(saves_dir, "/saves");

    dir = opendir(saves_dir);
    if (dir == NULL)
    {
        perror("Error opening directory: ");
        printf("Path: %s\n", saves_dir);
        return 1;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        // Check if the entry is a regular file with a .sav extension
        // Saves only: keep the .sav_bak backups and other side files.
        if (entry->d_type == DT_REG && has_sav_suffix(entry->d_name))
        {
            // Combine the base path and file name
            char full_path[strlen(saves_dir) + 1 + strlen(entry->d_name) + 1];
            sprintf(full_path, "%s/%s", saves_dir, entry->d_name);

            // Delete file
            remove(full_path);
            unlink(full_path);
        }
    }

    closedir(dir);

    // delete logs
    char logs_dir[MAX_FILE_PATH_CHAR];
    strcpy(logs_dir, config_path);
    strcat(logs_dir, "/logs");

    dir = opendir(logs_dir);
    if (dir == NULL)
    {
        perror("Error opening directory: ");
        printf("Path: %s\n", logs_dir);
        return 1;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        // Check if the entry is a regular file with a .sav extension
        if (entry->d_type == DT_REG && strstr(entry->d_name, ".txt"))
        {
            // Combine the base path and file name
            char full_path[strlen(logs_dir) + 1 + strlen(entry->d_name) + 1];
            sprintf(full_path, "%s/%s", logs_dir, entry->d_name);

            // Delete file
            remove(full_path);
            unlink(full_path);
        }
    }

    closedir(dir);

    // Delete saves/ directory
    int status = rmdir(saves_dir);
    if (status == -1)
    {
        perror("Error deleting saves directory: ");
        printf("Path: %s\n", saves_dir);
    }

    // Delete logs/ directory
    status = rmdir(logs_dir);
    if (status == -1)
    {
        perror("Error deleting logs directory: ");
        printf("Path: %s\n", logs_dir);
    }

    // Delete PokeromTrader/ directory
    status = rmdir(config_path);
    if (status == -1)
    {
        perror("Error deleting PokeromTrader directory: ");
        printf("Path: %s\n", config_path);
    }

    return 0;
}

void write_to_log(const char *msg, const uint8_t message_type)
{
    FILE *fp;

    char date[20];
    char time_str[20];
    time_t now = time(NULL);
    const struct tm *t = localtime(&now);

    strftime(date, sizeof(date) - 1, "%Y-%m-%d", t);
    strftime(time_str, sizeof(time_str) - 1, "%H:%M:%S", t);

    const char *kind;
    switch (message_type)
    {
    case E_LOG_MESSAGE_TYPE_ERROR:
        kind = "error-log@";
        break;
    case E_LOG_MESSAGE_TYPE_INFO:
        kind = "info-log@";
        break;
    default:
        kind = "log@";
        break;
    }

    // Logging must never take the app down: callers log in the middle of a
    // save or trade, and exiting there would abandon the operation half-done.
    // If the log can't be opened, report to stderr and carry on.
    const char *home = getenv("HOME");
    char cwd[MAX_FILE_PATH_CHAR];
    int len = snprintf(cwd, sizeof(cwd), "%s%s/logs/%s%s.txt",
                       home ? home : ".", USR_DATA_DIR, kind, date);
    fp = (len > 0 && len < (int)sizeof(cwd)) ? fopen(cwd, "a+") : NULL;

    if (fp == NULL)
    {
        fprintf(stderr, "PokeromTrader: could not open log file (%d): %s\n", errno, msg);
        return;
    }

    fputs(time_str, fp);
    fputs(": ", fp);
    fputs(msg, fp);
    fputs("\n", fp);

    fclose(fp);
}

void init_settings_from_config(struct save_file_data *save_file_data)
{
    // Read and save the saves file directory from config.ini
    char *config_save_path = read_key_from_config("SAVE_FILE_DIR");

    if (config_save_path != NULL)
    {
        snprintf((char *)save_file_data->save_dir, sizeof(save_file_data->save_dir), "%s", config_save_path);
    }
    else
    {
        strcpy((char *)save_file_data->save_dir, "DIR_NOT_SET");
    }

    // malloc'd from read_key_from_config
    free(config_save_path);
}

#endif

void free_filehelper_pointers(void)
{
    free(resolved_path);
    free(absolute_path);
}
