#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/types.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/wait.h>

long long info(const char *path) 
{
    struct stat sym_link;
    if(stat(path, &sym_link) == -1){
        perror("Unable to execute\n");
	exit(-1);
    }
    if(S_ISREG(sym_link.st_mode)){
        return sym_link.st_size;
    }
    else if(S_ISLNK(sym_link.st_mode)){
        char buff[1024];
        int length = readlink(path, buff, sizeof(buff));
        if(length == -1){
                perror("Unable to execute\n");
                exit(-1);
         }
         char path_of_sym[4096];
         snprintf(path_of_sym, sizeof(path_of_sym), "%s/%s", path, buff);
         return info(path_of_sym);
    }
    DIR *directory = opendir(path);
    if (directory == NULL) {
        perror("Unable to execute\n");
        exit(-1);
    }
    long long ans = 0;
    struct dirent *entry;
    struct stat st;
    if(stat(path, &st) == -1){
    	perror("Unable to execute\n");
	exit(-1);
    }
    if(stat(path, &st) == 0){
        ans += st.st_size;
    }
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry -> d_name, ".") == 0 || strcmp(entry -> d_name, "..") == 0) {
            continue;
        }
        char new_path[1024];
        snprintf(new_path, sizeof(new_path), "%s/%s", path, entry->d_name);
         if(stat(new_path, &st) == 0){
            if(entry -> d_type == DT_REG){
                ans += st.st_size;
                continue;
            }
            else if(entry -> d_type == DT_DIR){
                ans += info(new_path);
            }
            else if(entry -> d_type == DT_LNK){
                char buff[1024];
                int length = readlink(new_path, buff, sizeof(buff));
                if(length == -1){
                    perror("Unable to execute\n");
                    exit(-1);
                }
                char path_of_sym[4096];
                snprintf(path_of_sym, sizeof(path_of_sym), "%s/%s", path, buff);
                ans += info(path_of_sym);
            }
        }
    }
    closedir(directory);
    return ans;
}

int main(int argc, char *argv[]){
    char buffer[1024];
    DIR *directory = opendir(argv[1]);
    if(directory == NULL){
        perror("Unable to execute\n");
        exit(-1);
    }
    long long ans = 0;
    struct dirent *entry;
    struct stat st;
    if(stat(argv[1], &st) == 0){
          ans += st.st_size;
    }
    while((entry = readdir(directory)) != NULL){
        if(strcmp(entry -> d_name, ".") == 0 || strcmp(entry -> d_name, "..") == 0){
            continue;
        }
        char new_path[4096];
        snprintf(new_path, sizeof(new_path), "%s/%s", argv[1], entry -> d_name);
        if(stat(new_path, &st) == 0){
            if(entry -> d_type == DT_REG){
                ans += (long long)st.st_size;
                continue;
            }
            else if(entry -> d_type == DT_DIR){
                int fd[2];
                if(pipe(fd) == -1){
                    perror("Unable to execute\n");
                    exit(-1);
                }
                int pid = fork();
                if(pid == 0){
                    close(fd[0]);
                    dup2(fd[1], 1);
                    close(fd[1]);
                    //if(execl("./myDU", "myDU", new_path, (char*)NULL) == -1){
                      //  perror("Unable to execute\n");
                       // exit(-1);
                    //}
                    printf("%lld\n", info(new_path));
                }
                else{
                    close(fd[1]);
                    waitpid(pid, NULL, 0);
                    char buff[1024];
                    int bytes_read;
                    if((bytes_read = read(fd[0], buff, sizeof(buff) - 1)) == -1){
                        perror("Unable to execute\n");
                        exit(-1);
                    }
                    buff[bytes_read] = '\0';
                    ans += atoll(buff);
                    close(fd[0]);
                }
            }
            else if(entry -> d_type == DT_LNK){
                char buff[1024];
                int length = readlink(new_path, buff, sizeof(buff));
                if(length == -1){
                    perror("Unable to execute\n");
                    exit(-1);
                }
                char path_of_sym[4096];
                snprintf(path_of_sym, sizeof(path_of_sym), "%s/%s", argv[1], buff);
		ans += info(path_of_sym);
            }
        }
    }
    printf("%lld\n", ans);
    return 0;
}
