#include "utils.h"
#include <stdlib.h>

void error_exit(const char* msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

void free_string_array(char*** arr, int size) {
    for (int i = 0; i < size; i++) {
        char** sub_arr = arr[i];
        int j = 0;
        while (sub_arr[j] != NULL) {
            free(sub_arr[j]);
            j++;
        }
        free(sub_arr);
    }
}
