#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <utime.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <stdint.h>
#include <stddef.h>
#define BUFFER_SIZE 4096
#define MAX_REDUNDANCY 10


typedef struct {
    uint32_t crc;
    off_t offset;
    off_t size;
} FileCopyMeta;

typedef struct {
    char name[256];
    mode_t mode;
    uid_t uid;
    gid_t gid;
    time_t atime;
    time_t mtime;
    int copies;
    FileCopyMeta *copy_meta;
} FileMeta;

static uint32_t crc32_table[256];

// Инициализация таблицы CRC32
void init_crc32_table() {
    uint32_t polynomial = 0xEDB88320;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 8; j > 0; j--) {
            crc = (crc & 1) ? (crc >> 1) ^ polynomial : crc >> 1;
        }
        crc32_table[i] = crc;
    }
}


uint32_t calculate_crc32_buffer(const void *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t *bytes = (const uint8_t *)data;
    
    for (size_t i = 0; i < length; i++) {
        uint8_t table_index = (crc ^ bytes[i]) & 0xFF;
        crc = (crc >> 8) ^ crc32_table[table_index];
    }
    
    return crc ^ 0xFFFFFFFF;
}

// Расчет CRC32 для файла (потоковое чтение)
uint32_t calculate_crc32_file(FILE *file) {
    uint32_t crc = 0xFFFFFFFF;
    uint8_t buffer[BUFFER_SIZE];
    size_t bytes_read;

    fseek(file, 0, SEEK_SET);
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        crc = calculate_crc32_buffer(buffer, bytes_read);
    }
    
    return crc;
}

// Расчет CRC32 для файла с потоковым чтением
/*uint32_t calculate_crc32_file(FILE *file) {
    uint32_t crc = 0xFFFFFFFF;
    uint8_t buffer[BUFFER_SIZE];
    size_t bytes_read;

    fseek(file, 0, SEEK_SET);
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        for (size_t i = 0; i < bytes_read; i++) {
            uint8_t table_index = (crc ^ buffer[i]) & 0xFF;
            crc = (crc >> 8) ^ crc32_table[table_index];
        }
    }
    return crc ^ 0xFFFFFFFF;
}*/



// Глубокое копирование метаданных
FileMeta copy_metadata(const FileMeta *src) {
    FileMeta dst;
    memcpy(&dst, src, sizeof(FileMeta));
    dst.copy_meta = malloc(src->copies * sizeof(FileCopyMeta));
    memcpy(dst.copy_meta, src->copy_meta, src->copies * sizeof(FileCopyMeta));
    return dst;
}

// Освобождение памяти метаданных
void free_metadata(FileMeta *meta, int count) {
    for (int i = 0; i < count; i++) {
        if (meta[i].copy_meta) {
            free(meta[i].copy_meta);
        }
    }
    free(meta);
}

FileMeta* read_metadata(FILE *arch, int file_count) {
    FileMeta *meta_array = malloc(file_count * sizeof(FileMeta));
    for (int i = 0; i < file_count; i++) {
        fread(&meta_array[i], offsetof(FileMeta, copy_meta), 1, arch);
        meta_array[i].copy_meta = malloc(meta_array[i].copies * sizeof(FileCopyMeta));
        fread(meta_array[i].copy_meta, sizeof(FileCopyMeta), meta_array[i].copies, arch);
    }
    return meta_array;
}

// Запись метаданных
void write_metadata(FILE *arch, FileMeta *meta_array, int file_count) {
    for (int i = 0; i < file_count; i++) {
        fwrite(&meta_array[i], offsetof(FileMeta, copy_meta), 1, arch);
        fwrite(meta_array[i].copy_meta, sizeof(FileCopyMeta), meta_array[i].copies, arch);
    }
}

void verify_archive(const char *archive_name) {
    FILE *arch = fopen(archive_name, "rb");
    if (!arch) {
        perror("Ошибка открытия архива");
        return;
    }

    // Читаем количество файлов
    int file_count;
    if (fread(&file_count, sizeof(int), 1, arch) != 1) {
        printf("Ошибка чтения заголовка!\n");
        fclose(arch);
        return;
    }

    // Находим метаданные
    fseek(arch, -sizeof(long), SEEK_END);
    long meta_offset;
    fread(&meta_offset, sizeof(long), 1, arch);

    // Читаем метаданные
    fseek(arch, meta_offset, SEEK_SET);
    FileMeta *meta_array = read_metadata(arch, file_count);

    // Проверяем каждый файл
    for (int i = 0; i < file_count; i++) {
        printf("Проверка файла: %s\n", meta_array[i].name);
        for (int j = 0; j < meta_array[i].copies; j++) {
            fseek(arch, meta_array[i].copy_meta[j].offset, SEEK_SET);
            uint8_t *buf = malloc(meta_array[i].copy_meta[j].size);
            fread(buf, 1, meta_array[i].copy_meta[j].size, arch);

            uint32_t crc = calculate_crc32_buffer(buf, meta_array[i].copy_meta[j].size);
            if (crc != meta_array[i].copy_meta[j].crc) {
                printf("  Копия %d: ОШИБКА CRC (ожидалось %08x, получено %08x)\n", 
                      j+1, meta_array[i].copy_meta[j].crc, crc);
            } else {
                printf("  Копия %d: OK\n", j+1);
            }
            free(buf);
        }
    }

    free_metadata(meta_array, file_count);
    fclose(arch);
}
void create_archive(const char *archive_name, int file_count, char *files[], int redundancy) {
    FILE *arch = fopen(archive_name, "wb");
    fwrite(&file_count, sizeof(int), 1, arch);

    FileMeta *meta_array = malloc(file_count * sizeof(FileMeta));
    for (int i = 0; i < file_count; i++) {
        struct stat st;
        lstat(files[i], &st);
        strncpy(meta_array[i].name, files[i], 255);
        meta_array[i].mode = st.st_mode;
        meta_array[i].uid = st.st_uid;
        meta_array[i].gid = st.st_gid;
        meta_array[i].atime = st.st_atime;
        meta_array[i].mtime = st.st_mtime;
        meta_array[i].copies = redundancy;
        meta_array[i].copy_meta = malloc(redundancy * sizeof(FileCopyMeta));

        FILE *file = fopen(files[i], "rb");
        fseek(file, 0, SEEK_END);
        off_t file_size = ftell(file);
        fseek(file, 0, SEEK_SET);
        uint8_t *file_data = malloc(file_size);
        fread(file_data, 1, file_size, file);
        fclose(file);

        for (int j = 0; j < redundancy; j++) {
            meta_array[i].copy_meta[j].offset = ftell(arch);
            meta_array[i].copy_meta[j].size = file_size;
            meta_array[i].copy_meta[j].crc = calculate_crc32_buffer(file_data, file_size);
            fwrite(file_data, 1, file_size, arch);
        }
        free(file_data);
    }

    long meta_offset = ftell(arch);
    write_metadata(arch, meta_array, file_count);
    fwrite(&meta_offset, sizeof(long), 1, arch);
    free_metadata(meta_array, file_count);
    fclose(arch);
}

void delete_from_archive(const char *archive_name, const char *file_to_delete) {
    // Открываем исходный архив для чтения
    FILE *src_arch = fopen(archive_name, "rb");
    if (!src_arch) {
        perror("Ошибка открытия архива");
        exit(EXIT_FAILURE);
    }

    // Читаем общее количество файлов
    int total_files;
    fread(&total_files, sizeof(int), 1, src_arch);

    // Находим позицию метаданных
    fseek(src_arch, -sizeof(long), SEEK_END);
    long meta_offset;
    fread(&meta_offset, sizeof(long), 1, src_arch);

    // Читаем все метаданные
    fseek(src_arch, meta_offset, SEEK_SET);
    FileMeta *orig_meta = read_metadata(src_arch, total_files);
    fclose(src_arch);

    // Фильтруем файлы для сохранения
    FileMeta *new_meta = malloc(total_files * sizeof(FileMeta));
    int new_count = 0;
    int found = 0;

    // 1. Фильтрация метаданных
    for (int i = 0; i < total_files; i++) {
        if (strcmp(orig_meta[i].name, file_to_delete) == 0) {
            free(orig_meta[i].copy_meta);
            found = 1;
        } else {
            // Глубокое копирование метаданных
            new_meta[new_count] = orig_meta[i];
            new_meta[new_count].copy_meta = malloc(orig_meta[i].copies * sizeof(FileCopyMeta));
            memcpy(new_meta[new_count].copy_meta, 
                   orig_meta[i].copy_meta,
                   orig_meta[i].copies * sizeof(FileCopyMeta));
            new_count++;
        }
    }

    if (!found) {
        printf("Файл '%s' не найден!\n", file_to_delete);
        free_metadata(orig_meta, total_files);
        free(new_meta);
        return;
    }

    // Создаем временный архив
    char tmp_name[] = "temp_archXXXXXX";
    int tmp_fd = mkstemp(tmp_name);
    FILE *tmp_arch = fdopen(tmp_fd, "wb");

    // 2. Записыем новый заголовок (временно)
    fwrite(&new_count, sizeof(int), 1, tmp_arch);

    // 3. Копируем данные с обновлением смещений
    for (int i = 0; i < new_count; i++) {
        FILE *src = fopen(archive_name, "rb");
        
        // Для каждой копии файла
        for (int copy_num = 0; copy_num < new_meta[i].copies; copy_num++) {
            // Сохраняем оригинальные данные копии
            off_t orig_offset = new_meta[i].copy_meta[copy_num].offset;
            off_t size = new_meta[i].copy_meta[copy_num].size;

            // Обновляем смещение в новых метаданных
            new_meta[i].copy_meta[copy_num].offset = ftell(tmp_arch);

            // Копируем данные из исходного архива
            fseek(src, orig_offset, SEEK_SET);
            uint8_t buffer[BUFFER_SIZE];
            off_t remaining = size;
            
            while (remaining > 0) {
                size_t to_read = remaining > BUFFER_SIZE ? BUFFER_SIZE : remaining;
                size_t bytes_read = fread(buffer, 1, to_read, src);
                fwrite(buffer, 1, bytes_read, tmp_arch);
                remaining -= bytes_read;
            }
        }
        fclose(src);
    }

    // 4. Записываем обновленные метаданные
    long new_meta_offset = ftell(tmp_arch);
    write_metadata(tmp_arch, new_meta, new_count);
    fwrite(&new_meta_offset, sizeof(long), 1, tmp_arch);

    // 5. Финализируем операции
    fclose(tmp_arch);
    remove(archive_name);
    rename(tmp_name, archive_name);

    // 6. Освобождаем ресурсы
//    free_metadata(orig_meta, total_files);
//    free_metadata(new_meta, new_count);
    printf("Файл '%s' успешно удален\n", file_to_delete);
}

void add_to_archive(const char *archive_name, int new_file_count, char *new_files[], int redundancy) {
    /* Открываем архив для чтения метаданных */
    FILE *old_arch = fopen(archive_name, "rb");
    if(!old_arch) {
        perror("Ошибка открытия архива");
        exit(EXIT_FAILURE);
    }

    /* Читаем существующие данные */
    int old_file_count;
    fread(&old_file_count, sizeof(int), 1, old_arch);

    long old_meta_offset;
    fseek(old_arch, -sizeof(long), SEEK_END);
    fread(&old_meta_offset, sizeof(long), 1, old_arch);

    /* Читаем старые метаданные */
    fseek(old_arch, old_meta_offset, SEEK_SET);
    FileMeta *old_meta = read_metadata(old_arch, old_file_count);
    fclose(old_arch);

    /* Создаем временный архив */
    char tmp_name[L_tmpnam];
    mkstemp(tmp_name);
    FILE *new_arch = fopen(tmp_name, "wb");
    
    /* Записываем новый заголовок (временно, потом обновим) */
    int new_total = old_file_count + new_file_count;
    fwrite(&new_total, sizeof(int), 1, new_arch);

    /* Копируем данные старых файлов с новыми смещениями */
    for(int i = 0; i < old_file_count; i++) {
        FILE *src_arch = fopen(archive_name, "rb");
        for(int j = 0; j < old_meta[i].copies; j++) {
            /* Читаем старые данные */
            fseek(src_arch, old_meta[i].copy_meta[j].offset, SEEK_SET);
            uint8_t *buf = malloc(old_meta[i].copy_meta[j].size);
            fread(buf, 1, old_meta[i].copy_meta[j].size, src_arch);
            
            /* Обновляем смещение в метаданных */
            old_meta[i].copy_meta[j].offset = ftell(new_arch);
            
            /* Записываем в новый архив */
            fwrite(buf, 1, old_meta[i].copy_meta[j].size, new_arch);
            free(buf);
        }
        fclose(src_arch);
    }

    /* Добавляем новые файлы */
    FileMeta *new_meta = malloc(new_file_count * sizeof(FileMeta));
    for(int i = 0; i < new_file_count; i++) {
        struct stat st;
        if(lstat(new_files[i], &st) != 0) {
            perror("Ошибка получения метаданных");
            continue;
        }

        /* Заполняем метаданные */
        strncpy(new_meta[i].name, new_files[i], 255);
        new_meta[i].mode = st.st_mode;
        new_meta[i].uid = st.st_uid;
        new_meta[i].gid = st.st_gid;
        new_meta[i].atime = st.st_atime;
        new_meta[i].mtime = st.st_mtime;
        new_meta[i].copies = redundancy;
        new_meta[i].copy_meta = malloc(redundancy * sizeof(FileCopyMeta));

        /* Читаем данные файла */
        FILE *file = fopen(new_files[i], "rb");
        fseek(file, 0, SEEK_END);
        off_t file_size = ftell(file);
        fseek(file, 0, SEEK_SET);
        uint8_t *file_data = malloc(file_size);
        fread(file_data, 1, file_size, file);
        fclose(file);

        /* Записываем копии */
        for(int j = 0; j < redundancy; j++) {
            new_meta[i].copy_meta[j].offset = ftell(new_arch);
            new_meta[i].copy_meta[j].size = file_size;
            new_meta[i].copy_meta[j].crc = calculate_crc32_buffer(file_data, file_size);
            fwrite(file_data, 1, file_size, new_arch);
        }
        free(file_data);
    }

    /* Объединяем все метаданные */
    FileMeta *combined_meta = malloc(new_total * sizeof(FileMeta));
    memcpy(combined_meta, old_meta, old_file_count * sizeof(FileMeta));
    memcpy(&combined_meta[old_file_count], new_meta, new_file_count * sizeof(FileMeta));

    /* Записываем новые метаданные */
    long new_meta_offset = ftell(new_arch);
    write_metadata(new_arch, combined_meta, new_total);
    fwrite(&new_meta_offset, sizeof(long), 1, new_arch);

    /* Финализируем архив */
    fclose(new_arch);
    remove(archive_name);
    rename(tmp_name, archive_name);

    /* Освобождаем ресурсы */
    free_metadata(old_meta, old_file_count);
    free_metadata(new_meta, new_file_count);
    free(combined_meta);
}


/*uint32_t calculate_crc32_buffer(const void *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < length; i++) {
        uint8_t table_index = (crc ^ bytes[i]) & 0xFF;
        crc = (crc >> 8) ^ crc32_table[table_index];
    }
    return crc ^ 0xFFFFFFFF;
}*/

void extract_archive(const char *archive_name, const char *output_dir, const char *file_to_extract) {
    FILE *arch = fopen(archive_name, "rb");
    if (!arch) {
        perror("Ошибка открытия архива");
        exit(EXIT_FAILURE);
    }

    // Читаем количество файлов и избыточность
    int file_count, redundancy;
    fread(&file_count, sizeof(int), 1, arch);
    //fread(&redundancy, sizeof(int), 1, arch);

    // Читаем смещение метаданных
    fseek(arch, -sizeof(long), SEEK_END);
    long meta_offset;
    fread(&meta_offset, sizeof(long), 1, arch);

    // Переходим к метаданным
    fseek(arch, meta_offset, SEEK_SET);

    // Читаем метаданные
    FileMeta *meta_array = malloc(file_count * sizeof(FileMeta));
    if (!meta_array) {
        perror("Ошибка выделения памяти");
        fclose(arch);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < file_count; i++) {
        // Читаем основную часть структуры FileMeta (без copy_meta)
        fread(&meta_array[i], offsetof(FileMeta, copy_meta), 1, arch);

        // Выделяем память для массива copy_meta
        meta_array[i].copy_meta = malloc(meta_array[i].copies * sizeof(FileCopyMeta));
        if (!meta_array[i].copy_meta) {
            perror("Ошибка выделения памяти");
            fclose(arch);
            free(meta_array);
            exit(EXIT_FAILURE);
        }

        // Читаем массив copy_meta
        fread(meta_array[i].copy_meta, sizeof(FileCopyMeta), meta_array[i].copies, arch);
    }

    // Восстанавливаем файлы
    for (int i = 0; i < file_count; i++) {
        FileMeta *meta = &meta_array[i];

        // Если указан конкретный файл, пропускаем остальные
        if (file_to_extract && strcmp(meta->name, file_to_extract) != 0) {
            continue;
        }

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", output_dir, meta->name);

        // Проверяем, существует ли файл
        if (access(path, F_OK) == 0) {
            printf("Файл %s уже существует. Перезаписать? [y/N] ", path);
            int c = getchar();
            if (c != 'y' && c != 'Y') {
                continue; // Пропускаем файл, если пользователь не хочет перезаписывать
            }
        }

        int extracted = 0;
        for (int j = 0; j < meta->copies && !extracted; j++) {
            // Переходим к данным файла
            fseek(arch, meta->copy_meta[j].offset, SEEK_SET);

            // Читаем данные файла
            uint8_t *data = malloc(meta->copy_meta[j].size);
            if (!data) {
                perror("Ошибка выделения памяти");
                continue;
            }
            fread(data, 1, meta->copy_meta[j].size, arch);

            // Проверяем CRC32
            uint32_t actual_crc = calculate_crc32_buffer(data, meta->copy_meta[j].size);
            if (actual_crc == meta->copy_meta[j].crc) {
                // Создаем директорию, если она не существует
                char *dir_path = strdup(path);
                char *last_slash = strrchr(dir_path, '/');
                if (last_slash) {
                    *last_slash = '\0'; // Отделяем путь к директории
                    mkdir(dir_path, 0777); // Создаем директорию с правами 0777
                }
                free(dir_path);

                // Записываем данные в файл
                FILE *out = fopen(path, "wb");
                if (!out) {
                    perror("Ошибка создания файла");
                    free(data);
                    continue;
                }
                fwrite(data, 1, meta->copy_meta[j].size, out);
                fclose(out);

                // Восстанавливаем метаданные
                chmod(path, meta->mode);
                chown(path, meta->uid, meta->gid);
                struct utimbuf times = {meta->atime, meta->mtime};
                utime(path, &times);

                extracted = 1;
                printf("Файл %s восстановлен из копии %d\n", path, j + 1);
            }
            free(data);
        }
        if (!extracted) {
            printf("ОШИБКА: Все копии файла %s повреждены!\n", path);
        }
    }

    // Освобождаем память
    for (int i = 0; i < file_count; i++) {
        free(meta_array[i].copy_meta);
    }
    free(meta_array);
    fclose(arch);
}

void list_archive(const char *archive_name) {
    FILE *arch = fopen(archive_name, "rb");
    if (!arch) {
        perror("Ошибка открытия архива");
        exit(EXIT_FAILURE);
    }

    // Читаем количество файлов и избыточность
    int file_count, redundancy;
    fread(&file_count, sizeof(int), 1, arch);
    //fread(&redundancy, sizeof(int), 1, arch);

    // Читаем смещение метаданных
    fseek(arch, -sizeof(long), SEEK_END);
    long meta_offset;
    fread(&meta_offset, sizeof(long), 1, arch);

    // Переходим к метаданным
    fseek(arch, meta_offset, SEEK_SET);

    // Читаем метаданные
    FileMeta *meta_array = malloc(file_count * sizeof(FileMeta));
    if (!meta_array) {
        perror("Ошибка выделения памяти");
        fclose(arch);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < file_count; i++) {
        // Читаем основную часть структуры FileMeta (без copy_meta)
        fread(&meta_array[i], offsetof(FileMeta, copy_meta), 1, arch);

        // Выделяем память для массива copy_meta
        meta_array[i].copy_meta = malloc(meta_array[i].copies * sizeof(FileCopyMeta));
        if (!meta_array[i].copy_meta) {
            perror("Ошибка выделения памяти");
            fclose(arch);
            free(meta_array);
            exit(EXIT_FAILURE);
        }

        // Читаем массив copy_meta
        fread(meta_array[i].copy_meta, sizeof(FileCopyMeta), meta_array[i].copies, arch);
    }

    // Выводим метаданные
    printf("Архив: %s\n", archive_name);
    printf("Файлов: %d\n", file_count);
    //printf("Избыточность: %d\n", redundancy);
    printf("========================================\n");
    for (int i = 0; i < file_count; i++) {
        FileMeta *meta = &meta_array[i];
        printf("Файл: %s\n", meta->name);
        printf("Права: %o\n", meta->mode);
        printf("Владелец: %d\n", meta->uid);
        printf("Группа: %d\n", meta->gid);
        printf("Копий: %d\n", meta->copies);

        for (int j = 0; j < meta->copies; j++) {
            printf("  Копия %d:\n", j + 1);
            printf("    CRC32: %08x\n", meta->copy_meta[j].crc);
            printf("    Размер: %ld байт\n", (long)meta->copy_meta[j].size);
            printf("    Смещение: %ld\n", (long)meta->copy_meta[j].offset);
        }
        printf("----------------------------------------\n");
    }

    // Освобождаем память
    for (int i = 0; i < file_count; i++) {
        free(meta_array[i].copy_meta);
    }
    free(meta_array);
    fclose(arch);
}

int main(int argc, char *argv[]) {
    init_crc32_table();
    if (argc < 3) {
        printf("Использование:\n");
        printf("Упаковка: %s -c <архив> -b <избыточность> <файлы...>\n", argv[0]);
        printf("Удаление: %s -d <архив> <файл>\n", argv[0]);
        printf("Верификация: %s -v <архив>\n", argv[0]);
        printf("Добавление: %s -a <архив> -b <избыточность> <файлы...>\n", argv[0]);
        printf("Распаковка: %s -x <архив> <директория> [-f <файл>]\n", argv[0]);
        printf("Список: %s -l <архив>\n", argv[0]);
        return 0;
    }
    if (strcmp(argv[1], "-c") == 0) {
        if (argc < 5 || strcmp(argv[3], "-b") != 0) {
            printf("Ошибка: Укажите избыточность через -b\n");
            return 1;
        }
        int redundancy = atoi(argv[4]);
        if (redundancy < 1 || redundancy > MAX_REDUNDANCY) {
            printf("Некорректная избыточность (1-%d)\n", MAX_REDUNDANCY);
            return 1;
        }
        create_archive(argv[2], argc - 5, &argv[5], redundancy);
    } else  if (argc > 2 && strcmp(argv[1], "-d") == 0) {
        delete_from_archive(argv[2], argv[3]);
        verify_archive(argv[2]);
        return 0;
    } else  if (argc > 2 && strcmp(argv[1], "-v") == 0) {
        verify_archive(argv[2]);
        return 0;
    } else if (strcmp(argv[1], "-a") == 0) {
        if (argc < 5 || strcmp(argv[3], "-b") != 0) {
            printf("Ошибка: Укажите избыточность через -b\n");
            return 1;
        }
        int redundancy = atoi(argv[4]);
        if (redundancy < 1 || redundancy > MAX_REDUNDANCY) {
            printf("Некорректная избыточность (1-%d)\n", MAX_REDUNDANCY);
            return 1;
        }
        add_to_archive(argv[2], argc - 5, &argv[5], redundancy);
    } else if (strcmp(argv[1], "-x") == 0) {
        if (argc < 4) {
            printf("Укажите выходную директорию\n");
            return 1;
        }
        const char *file_to_extract = NULL;
        if (argc > 5 && strcmp(argv[4], "-f") == 0) {
            file_to_extract = argv[5];
        }
        extract_archive(argv[2], argv[3], file_to_extract);
    } else if (strcmp(argv[1], "-l") == 0) {
        list_archive(argv[2]);
    } else {
        printf("Неизвестная команда\n");
    }
    return 0;
}
