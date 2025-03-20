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


// Узел дерева Хаффмана
typedef struct HuffmanNode {
    char symbol;
    int frequency;
    struct HuffmanNode *left, *right;
} HuffmanNode;

// Очередь с приоритетом для построения дерева Хаффмана
typedef struct PriorityQueue {
    int size;
    HuffmanNode **nodes;
} PriorityQueue;

// Создание нового узла
HuffmanNode *create_node(char symbol, int frequency) {
    HuffmanNode *node = (HuffmanNode *)malloc(sizeof(HuffmanNode));
    node->symbol = symbol;
    node->frequency = frequency;
    node->left = node->right = NULL;
    return node;
}

// Создание очереди с приоритетом
PriorityQueue *create_queue(int capacity) {
    PriorityQueue *queue = (PriorityQueue *)malloc(sizeof(PriorityQueue));
    queue->size = 0;
    queue->nodes = (HuffmanNode **)malloc(capacity * sizeof(HuffmanNode *));
    return queue;
}

// Добавление узла в очередь
void enqueue(PriorityQueue *queue, HuffmanNode *node) {
    int i = queue->size++;
    while (i > 0 && queue->nodes[(i - 1) / 2]->frequency > node->frequency) {
        queue->nodes[i] = queue->nodes[(i - 1) / 2];
        i = (i - 1) / 2;
    }
    queue->nodes[i] = node;
}

// Извлечение узла с минимальной частотой
HuffmanNode *dequeue(PriorityQueue *queue) {
    HuffmanNode *min_node = queue->nodes[0];
    queue->nodes[0] = queue->nodes[--queue->size];
    int i = 0;
    while (1) {
        int left = 2 * i + 1;
        int right = 2 * i + 2;
        int smallest = i;
        if (left < queue->size && queue->nodes[left]->frequency < queue->nodes[smallest]->frequency)
            smallest = left;
        if (right < queue->size && queue->nodes[right]->frequency < queue->nodes[smallest]->frequency)
            smallest = right;
        if (smallest == i) break;
        HuffmanNode *temp = queue->nodes[i];
        queue->nodes[i] = queue->nodes[smallest];
        queue->nodes[smallest] = temp;
        i = smallest;
    }
    return min_node;
}

// Построение дерева Хаффмана
HuffmanNode *build_huffman_tree(int *frequencies) {
    PriorityQueue *queue = create_queue(256);
    for (int i = 0; i < 256; i++) {
        if (frequencies[i] > 0) {
            enqueue(queue, create_node((char)i, frequencies[i]));
        }
    }
    while (queue->size > 1) {
        HuffmanNode *left = dequeue(queue);
        HuffmanNode *right = dequeue(queue);
        HuffmanNode *parent = create_node('\0', left->frequency + right->frequency);
        parent->left = left;
        parent->right = right;
        enqueue(queue, parent);
    }
    HuffmanNode *root = dequeue(queue);
    free(queue->nodes);
    free(queue);
    return root;
}

// Генерация кодов Хаффмана
void generate_codes(HuffmanNode *root, char *code, int depth, char **codes) {
    if (root->left == NULL && root->right == NULL) {
        code[depth] = '\0';
        codes[(unsigned char)root->symbol] = strdup(code);
        return;
    }
    code[depth] = '0';
    generate_codes(root->left, code, depth + 1, codes);
    code[depth] = '1';
    generate_codes(root->right, code, depth + 1, codes);
}

// Сериализация дерева Хаффмана
void serialize_tree(HuffmanNode *root, FILE *output) {
    if (root == NULL) {
        fputc('0', output);
        return;
    }
    fputc('1', output);
    fputc(root->symbol, output);
    serialize_tree(root->left, output);
    serialize_tree(root->right, output);
}

// Десериализация дерева Хаффмана
HuffmanNode *deserialize_tree(FILE *input) {
    int flag = fgetc(input); // Читаем флаг (1 или 0)
    if (flag == EOF) {
        return NULL; // Ошибка чтения
    }
    if (flag == '0') {
        return NULL; // Конец ветви
    }

    int symbol = fgetc(input); // Читаем символ
    if (symbol == EOF) {
        return NULL; // Ошибка чтения
    }

    // Создаем узел
    HuffmanNode *node = create_node((char)symbol, 0);

    // Рекурсивно восстанавливаем левое и правое поддеревья
    node->left = deserialize_tree(input);
    node->right = deserialize_tree(input);

    return node;
}

// Сжатие файла
void compress_file(const char *input_file, const char *output_file) {
    FILE *input = fopen(input_file, "rb");
    if (!input) {
        perror("Ошибка открытия входного файла");
        exit(EXIT_FAILURE);
    }

    // Подсчет частот символов
    int frequencies[256] = {0};
    char ch;
    while (fread(&ch, sizeof(char), 1, input) == 1) {
        frequencies[(unsigned char)ch]++;
    }
    fseek(input, 0, SEEK_SET);

    // Построение дерева Хаффмана
    HuffmanNode *root = build_huffman_tree(frequencies);

    // Генерация кодов
    char *codes[256] = {NULL};
    char code[256];
    generate_codes(root, code, 0, codes);

    // Запись сжатых данных
    if (access(output_file, F_OK) == 0) {
        printf("Файл %s уже существует. Перезаписать? [y/N] ", input_file);
        int c = getchar();
        // Очищаем буфер ввода
        while (getchar() != '\n');
        if (c != 'y' && c != 'Y') {
            return; // Пропускаем файл, если пользователь не хочет перезаписывать
        }
    }
    FILE *output = fopen(output_file, "wb");
    if (!output) {
        perror("Ошибка создания выходного файла");
        fclose(input);
        exit(EXIT_FAILURE);
    }

    // Сериализация дерева Хаффмана
    serialize_tree(root, output);

    // Кодирование данных
    unsigned char buffer = 0;
    int bit_count = 0;
    while (fread(&ch, sizeof(char), 1, input) == 1) {
        char *huffman_code = codes[(unsigned char)ch];
        for (int i = 0; huffman_code[i]; i++) {
            buffer <<= 1;
            if (huffman_code[i] == '1') buffer |= 1;
            bit_count++;
            if (bit_count == 8) {
                fwrite(&buffer, sizeof(char), 1, output);
                buffer = 0;
                bit_count = 0;
            }
        }
    }
    if (bit_count > 0) {
        buffer <<= (8 - bit_count);
        fwrite(&buffer, sizeof(char), 1, output);
    }

    fclose(input);
    fclose(output);

    printf("Файл успешно сжат: %s -> %s\n", input_file, output_file);
}

// Распаковка файла
void decompress_file(const char *input_file, const char *output_file) {
    FILE *input = fopen(input_file, "rb");
    if (!input) {
        perror("Ошибка открытия входного файла");
        exit(EXIT_FAILURE);
    }

    // Десериализация дерева Хаффмана
    HuffmanNode *root = deserialize_tree(input);
    if (!root) {
        perror("Ошибка десериализации дерева Хаффмана");
        fclose(input);
        exit(EXIT_FAILURE);
    }

    // Открытие выходного файла
    if (access(output_file, F_OK) == 0) {
        printf("Файл %s уже существует. Перезаписать? [y/N] ", input_file);
        int c = getchar();
        // Очищаем буфер ввода
        while (getchar() != '\n');
        if (c != 'y' && c != 'Y') {
            return; // Пропускаем файл, если пользователь не хочет перезаписывать
        }
    }

    
    FILE *output = fopen(output_file, "wb");
    if (!output) {
        perror("Ошибка создания выходного файла");
        fclose(input);
        exit(EXIT_FAILURE);
    }

    // Декодирование данных
    HuffmanNode *current = root;
    unsigned char buffer;
    int bit_count = 0;
    while (fread(&buffer, sizeof(char), 1, input) == 1) {
        for (int i = 7; i >= 0; i--) {
            int bit = (buffer >> i) & 1;
            if (bit == 0) {
                current = current->left;
            } else {
                current = current->right;
            }
            if (current->left == NULL && current->right == NULL) {
                fputc(current->symbol, output);
                current = root;
            }
        }
    }

    fclose(input);
    fclose(output);

    printf("Файл успешно распакован: %s -> %s\n", input_file, output_file);
}


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

    // Читаем смещение метаданных из начала файла
    long meta_offset;
    if (fread(&meta_offset, sizeof(long), 1, arch) != 1) {
        printf("Ошибка чтения смещения метаданных!\n");
        fclose(arch);
        return;
    }

    // Читаем количество файлов
    int file_count;
    if (fread(&file_count, sizeof(int), 1, arch) != 1) {
        printf("Ошибка чтения количества файлов!\n");
        fclose(arch);
        return;
    }

    // Переходим к метаданным
    fseek(arch, meta_offset, SEEK_SET);

    // Читаем метаданные
    FileMeta *meta_array = read_metadata(arch, file_count);
    if (!meta_array) {
        printf("Ошибка чтения метаданных!\n");
        fclose(arch);
        return;
    }

    // Проверяем каждый файл
    for (int i = 0; i < file_count; i++) {
        printf("Проверка файла: %s\n", meta_array[i].name);

        for (int j = 0; j < meta_array[i].copies; j++) {
            // Переходим к данным копии файла
            fseek(arch, meta_array[i].copy_meta[j].offset, SEEK_SET);

            // Читаем данные
            uint8_t *data = malloc(meta_array[i].copy_meta[j].size);
            if (!data) {
                perror("Ошибка выделения памяти");
                continue;
            }
            fread(data, 1, meta_array[i].copy_meta[j].size, arch);

            // Вычисляем CRC32 для данных
            uint32_t calculated_crc = calculate_crc32_buffer(data, meta_array[i].copy_meta[j].size);

            // Сравниваем с CRC32 из метаданных
            if (calculated_crc == meta_array[i].copy_meta[j].crc) {
                printf("  Копия %d: OK (CRC32: %08x)\n", j + 1, calculated_crc);
            } else {
                printf("  Копия %d: ОШИБКА (ожидалось: %08x, получено: %08x)\n",
                       j + 1, meta_array[i].copy_meta[j].crc, calculated_crc);
            }

            free(data);
        }
    }

    // Освобождаем память
    free_metadata(meta_array, file_count);
    fclose(arch);
}

void extract_archive(const char *archive_name, const char *output_dir, const char *file_to_extract) {
    FILE *arch = fopen(archive_name, "rb");
    if (!arch) {
        perror("Ошибка открытия архива");
        exit(EXIT_FAILURE);
    }

    // Читаем смещение метаданных
    long meta_offset;
    fread(&meta_offset, sizeof(long), 1, arch);

    // Читаем количество файлов
    int file_count;
    fread(&file_count, sizeof(int), 1, arch);

    // Переходим к метаданным
    fseek(arch, meta_offset, SEEK_SET);
    FileMeta *meta_array = read_metadata(arch, file_count);

    // Восстанавливаем файлы
    int c;
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
            //c = getchar();
            while (getchar() != '\n');
            //if (c == 'y' || c != 'Y' || c != 'n' || c != 'N')
            if (c != 'y' && c != 'Y') {
                continue; // Пропускаем файл, если пользователь не хочет перезаписывать
            }
        }
	
        int extracted = 0;
        for (int j = 0; j < meta->copies && !extracted; j++) {
            // Переходим к данным файла
            fseek(arch, meta->copy_meta[j].offset, SEEK_SET);

            // Выделяем память для данных
            uint8_t *data = malloc(meta->copy_meta[j].size);
            if (!data) {
                perror("Ошибка выделения памяти");
                continue;
            }

            // Читаем данные
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

    // Читаем смещение метаданных
    long meta_offset;
    fread(&meta_offset, sizeof(long), 1, arch);

    // Читаем количество файлов
    int file_count;
    fread(&file_count, sizeof(int), 1, arch);

    // Переходим к метаданным
    fseek(arch, meta_offset, SEEK_SET);
    FileMeta *meta_array = read_metadata(arch, file_count);

    // Выводим информацию
    printf("Архив: %s\n", archive_name);
    printf("Файлов: %d\n", file_count);
    for (int i = 0; i < file_count; i++) {
        printf("Файл: %s\n", meta_array[i].name);
        printf("Копий: %d\n", meta_array[i].copies);
        for (int j = 0; j < meta_array[i].copies; j++) {
            printf("  Копия %d: CRC32=%08x, Размер=%ld, Смещение=%ld\n",
                   j + 1, meta_array[i].copy_meta[j].crc,
                   (long)meta_array[i].copy_meta[j].size,
                   (long)meta_array[i].copy_meta[j].offset);
        }
    }

    fclose(arch);
    free_metadata(meta_array, file_count);
}
void create_archive(const char *archive_name, int file_count, char *files[], int redundancy) {
    FILE *arch = fopen(archive_name, "wb");
    if (!arch) {
        perror("Ошибка открытия архива");
        exit(EXIT_FAILURE);
    }

    // Записываем временное значение смещения метаданных (0)
    long meta_offset = 0;
    fwrite(&meta_offset, sizeof(long), 1, arch);

    // Записываем количество файлов
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

    // Записываем метаданные
    meta_offset = ftell(arch); // Текущая позиция — это начало метаданных
    write_metadata(arch, meta_array, file_count);

    // Обновляем смещение метаданных в начале файла
    fseek(arch, 0, SEEK_SET);
    fwrite(&meta_offset, sizeof(long), 1, arch);

    free_metadata(meta_array, file_count);
    fclose(arch);
}

void delete_from_archive(const char *archive_name, const char *file_to_delete) {
    FILE *src_arch = fopen(archive_name, "rb");
    if (!src_arch) {
        perror("Ошибка открытия архива");
        exit(EXIT_FAILURE);
    }

    // Читаем смещение метаданных
    long meta_offset;
    fread(&meta_offset, sizeof(long), 1, src_arch);

    // Читаем количество файлов
    int total_files;
    fread(&total_files, sizeof(int), 1, src_arch);

    // Переходим к метаданным
    fseek(src_arch, meta_offset, SEEK_SET);
    FileMeta *orig_meta = read_metadata(src_arch, total_files);
    fclose(src_arch);

    // Фильтруем файлы для сохранения
    FileMeta *new_meta = malloc(total_files * sizeof(FileMeta));
    int new_count = 0;
    int found = 0;

    for (int i = 0; i < total_files; i++) {
        // Сравниваем имена файлов
        if (strcmp(orig_meta[i].name, file_to_delete) == 0) {
            printf("Файл '%s' найден для удаления.\n", file_to_delete);
            free(orig_meta[i].copy_meta); // Освобождаем память для копий
            found = 1;
        } else {
            // Копируем метаданные в новый массив
            new_meta[new_count] = orig_meta[i];
            new_meta[new_count].copy_meta = malloc(orig_meta[i].copies * sizeof(FileCopyMeta));
            memcpy(new_meta[new_count].copy_meta, orig_meta[i].copy_meta, orig_meta[i].copies * sizeof(FileCopyMeta));
            new_count++;
        }
    }

    if (!found) {
        printf("Файл '%s' не найден в архиве!\n", file_to_delete);
        free_metadata(orig_meta, total_files);
        free(new_meta);
        return;
    }

    // Создаем временный архив
    char tmp_name[] = "temp_archXXXXXX";
    int tmp_fd = mkstemp(tmp_name);
    FILE *tmp_arch = fdopen(tmp_fd, "wb");

    // Записываем временное значение смещения метаданных (0)
    long new_meta_offset = 0;
    fwrite(&new_meta_offset, sizeof(long), 1, tmp_arch);

    // Записываем новое количество файлов
    fwrite(&new_count, sizeof(int), 1, tmp_arch);

    // Копируем данные с обновлением смещений
    for (int i = 0; i < new_count; i++) {
        FILE *src = fopen(archive_name, "rb");
        for (int copy_num = 0; copy_num < new_meta[i].copies; copy_num++) {
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

    // Записываем новые метаданные
    new_meta_offset = ftell(tmp_arch);
    write_metadata(tmp_arch, new_meta, new_count);

    // Обновляем смещение метаданных в начале файла
    fseek(tmp_arch, 0, SEEK_SET);
    fwrite(&new_meta_offset, sizeof(long), 1, tmp_arch);

    // Финализируем операции
    fclose(tmp_arch);
    remove(archive_name);
    rename(tmp_name, archive_name);

    // Освобождаем память
    //free_metadata(orig_meta, total_files);
    //free_metadata(new_meta, new_count);

    printf("Файл '%s' успешно удален из архива.\n", file_to_delete);
}


void add_to_archive(const char *archive_name, int new_file_count, char *new_files[], int redundancy) {
    FILE *old_arch = fopen(archive_name, "rb");
    if (!old_arch) {
        perror("Ошибка открытия архива");
        exit(EXIT_FAILURE);
    }

    // Читаем смещение метаданных
    long old_meta_offset;
    fread(&old_meta_offset, sizeof(long), 1, old_arch);

    // Читаем количество файлов
    int old_file_count;
    fread(&old_file_count, sizeof(int), 1, old_arch);

    // Читаем старые метаданные
    fseek(old_arch, old_meta_offset, SEEK_SET);
    FileMeta *old_meta = read_metadata(old_arch, old_file_count);
    fclose(old_arch);

    // Создаем временный архив
    char tmp_name[] = "temp_archXXXXXX";
    int tmp_fd = mkstemp(tmp_name);
    FILE *new_arch = fdopen(tmp_fd, "wb");

    // Записываем временное значение смещения метаданных (0)
    long new_meta_offset = 0;
    fwrite(&new_meta_offset, sizeof(long), 1, new_arch);

    // Записываем новое количество файлов
    int new_total = old_file_count + new_file_count;
    fwrite(&new_total, sizeof(int), 1, new_arch);

    // Копируем данные старых файлов с новыми смещениями
    for (int i = 0; i < old_file_count; i++) {
        FILE *src_arch = fopen(archive_name, "rb");
        for (int j = 0; j < old_meta[i].copies; j++) {
            off_t orig_offset = old_meta[i].copy_meta[j].offset;
            off_t size = old_meta[i].copy_meta[j].size;

            old_meta[i].copy_meta[j].offset = ftell(new_arch);

            fseek(src_arch, orig_offset, SEEK_SET);
            uint8_t buffer[BUFFER_SIZE];
            off_t remaining = size;
            while (remaining > 0) {
                size_t to_read = remaining > BUFFER_SIZE ? BUFFER_SIZE : remaining;
                size_t bytes_read = fread(buffer, 1, to_read, src_arch);
                fwrite(buffer, 1, bytes_read, new_arch);
                remaining -= bytes_read;
            }
        }
        fclose(src_arch);
    }

    // Добавляем новые файлы
    FileMeta *new_meta = malloc(new_file_count * sizeof(FileMeta));
    for (int i = 0; i < new_file_count; i++) {
        struct stat st;
        if (lstat(new_files[i], &st) != 0) {
            perror("Ошибка получения метаданных");
            continue;
        }

        strncpy(new_meta[i].name, new_files[i], 255);
        new_meta[i].mode = st.st_mode;
        new_meta[i].uid = st.st_uid;
        new_meta[i].gid = st.st_gid;
        new_meta[i].atime = st.st_atime;
        new_meta[i].mtime = st.st_mtime;
        new_meta[i].copies = redundancy;
        new_meta[i].copy_meta = malloc(redundancy * sizeof(FileCopyMeta));

        FILE *file = fopen(new_files[i], "rb");
        fseek(file, 0, SEEK_END);
        off_t file_size = ftell(file);
        fseek(file, 0, SEEK_SET);
        uint8_t *file_data = malloc(file_size);
        fread(file_data, 1, file_size, file);
        fclose(file);

        for (int j = 0; j < redundancy; j++) {
            new_meta[i].copy_meta[j].offset = ftell(new_arch);
            new_meta[i].copy_meta[j].size = file_size;
            new_meta[i].copy_meta[j].crc = calculate_crc32_buffer(file_data, file_size);
            fwrite(file_data, 1, file_size, new_arch);
        }
        free(file_data);
    }

    // Объединяем метаданные
    FileMeta *combined_meta = malloc(new_total * sizeof(FileMeta));
    memcpy(combined_meta, old_meta, old_file_count * sizeof(FileMeta));
    memcpy(&combined_meta[old_file_count], new_meta, new_file_count * sizeof(FileMeta));

    // Записываем новые метаданные
    new_meta_offset = ftell(new_arch);
    write_metadata(new_arch, combined_meta, new_total);

    // Обновляем смещение метаданных в начале файла
    fseek(new_arch, 0, SEEK_SET);
    fwrite(&new_meta_offset, sizeof(long), 1, new_arch);

    // Финализируем операции
    fclose(new_arch);
    remove(archive_name);
    rename(tmp_name, archive_name);

    free_metadata(old_meta, old_file_count);
    free_metadata(new_meta, new_file_count);
    free(combined_meta);
}

void extract_metadata(const char *archive_name, const char *output_meta_file) {
    FILE *arch = fopen(archive_name, "rb");
    if (!arch) {
        perror("Ошибка открытия архива");
        exit(EXIT_FAILURE);
    }

    // Читаем смещение метаданных из начала файла
    long meta_offset;
    fread(&meta_offset, sizeof(long), 1, arch);

    // Читаем количество файлов
    int file_count;
    fread(&file_count, sizeof(int), 1, arch);

    // Переходим к метаданным
    fseek(arch, meta_offset, SEEK_SET);
    FileMeta *meta_array = read_metadata(arch, file_count);

    // Записываем метаданные в файл
    FILE *meta_file = fopen(output_meta_file, "wb");
    if (!meta_file) {
        perror("Ошибка создания файла метаданных");
        fclose(arch);
        free_metadata(meta_array, file_count);
        exit(EXIT_FAILURE);
    }

    // Записываем количество файлов
    fwrite(&file_count, sizeof(int), 1, meta_file);

    // Записываем метаданные
    write_metadata(meta_file, meta_array, file_count);

    fclose(meta_file);
    fclose(arch);
    free_metadata(meta_array, file_count);

    printf("Метаданные успешно извлечены в файл: %s\n", output_meta_file);
}

void load_metadata(const char *archive_name, const char *input_meta_file) {
    // Открываем архив для чтения и записи
    FILE *arch = fopen(archive_name, "r+b");
    if (!arch) {
        perror("Ошибка открытия архива");
        exit(EXIT_FAILURE);
    }

    // Читаем текущее смещение метаданных
    long old_meta_offset;
    fread(&old_meta_offset, sizeof(long), 1, arch);

    // Читаем количество файлов
    int old_file_count;
    fread(&old_file_count, sizeof(int), 1, arch);

    // Открываем файл метаданных для чтения
    FILE *meta_file = fopen(input_meta_file, "rb");
    if (!meta_file) {
        perror("Ошибка открытия файла метаданных");
        fclose(arch);
        exit(EXIT_FAILURE);
    }

    // Читаем количество файлов из файла метаданных
    int new_file_count;
    fread(&new_file_count, sizeof(int), 1, meta_file);

    // Читаем новые метаданные
    FileMeta *new_meta_array = read_metadata(meta_file, new_file_count);
    fclose(meta_file);

    // Переходим к месту, где начинаются метаданные
    fseek(arch, old_meta_offset, SEEK_SET);

    // Записываем новые метаданные
    long new_meta_offset = ftell(arch);
    write_metadata(arch, new_meta_array, new_file_count);

    // Обновляем смещение метаданных в начале файла
    fseek(arch, 0, SEEK_SET);
    fwrite(&new_meta_offset, sizeof(long), 1, arch);

    fclose(arch);
    free_metadata(new_meta_array, new_file_count);

    printf("Метаданные успешно загружены из файла: %s\n", input_meta_file);
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
        printf("\n");
        printf("Tests funtions:\n");
        printf("Извлечение метаданных: %s -mx <архив> <выходной_файл_метаданных>\n", argv[0]);
        printf("Загрузка метаданных: %s -ma <архив> <входной_файл_метаданных>\n", argv[0]);
        printf("\n");
        printf("Сжатие: %s -p <входной_файл> <выходной_файл>\n", argv[0]);
        printf("Распаковка: %s -u <входной_файл> <выходной_файл>\n", argv[0]);
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
    } else if (strcmp(argv[1], "-d") == 0) {
        delete_from_archive(argv[2], argv[3]);
    } else if (strcmp(argv[1], "-v") == 0) {
        verify_archive(argv[2]);
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
    } else if (strcmp(argv[1], "-mx") == 0) {
        if (argc < 4) {
            printf("Укажите выходной файл для метаданных\n");
            return 1;
        }
        extract_metadata(argv[2], argv[3]);
    } else if (strcmp(argv[1], "-ma") == 0) {
        if (argc < 4) {
            printf("Укажите входной файл метаданных\n");
            return 1;
        }
        load_metadata(argv[2], argv[3]);
    }else if (strcmp(argv[1], "-p") == 0) {
        compress_file(argv[2], argv[3]);
    } else if (strcmp(argv[1], "-u") == 0) {
        decompress_file(argv[2], argv[3]);
    } else {
        printf("Неизвестная команда\n");
    }
    return 0;
}
