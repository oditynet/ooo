# ooo
This is a file packer with replica support. You add any files to the archive and assign the number of replicas. Each replica calculates the checksum, file rights and size. You can view the contents of the archive or delete a file from the archive. The most important thing.... If your disk sector is broken and after restoring the file system the sector has changed its contents, then when extracting the file from the archive, the checksum of the replica is checked and if it is broken, the file is extracted from the next replica.

Это упаковщик файлов с поддержкой реплик. Вы добавляете в архив любые файлы и назначаете колчиство релик. Каждая реплика подсчитывае контрольную сумму,права на файл и размер. Вы можете посмотреть содержимое архива или удалить файл из архива.
Самое важное....Если у вас бьется сектор диска и после восстановления файловой системы сектор изменил свое содержание, то при извлекании их архива файла проверяется контрольная сумма реплики и если она битая,то извлекается файл из следующей реплики.

# Build
gcc ooo.c -o ooo

# Helper
```
./ooo
Использование:
Упаковка: ./16 -c <архив> -b <избыточность> <файлы...>
Удаление: ./16 -d <архив> <файл>
Верификация: ./16 -v <архив>
Добавление: ./16 -a <архив> -b <избыточность> <файлы...>
Распаковка: ./16 -x <архив> <директория> [-f <файл>]
Список: ./16 -l <архив>
```

Create pack with 2 files: t1 - repl 2, t2 - repl 1 
```
ooo -c out.ooo -b 2 t1 
ooo -a out.ooo -b 1 t2
ooo -l out.ooo        
Архив: out.ooo
Файлов: 2
========================================
Файл: t1
Права: 100644
Владелец: 1000
Группа: 1000
Копий: 2
  Копия 1:
    CRC32: d39177d1
    Размер: 6 байт
    Смещение: 4
  Копия 2:
    CRC32: d39177d1
    Размер: 6 байт
    Смещение: 10
----------------------------------------
Файл: t2
Права: 100644
Владелец: 1000
Группа: 1000
Копий: 1
  Копия 1:
    CRC32: 1564effa
    Размер: 4 байт
    Смещение: 16
----------------------------------------
```

Crash replic 1 for file t1
```
hexedit out.ooo 0x6 0x0
Байт по смещению 0x6 был 0x0a
Байт по смещению 0x6 успешно изменен на 0x00
```

Chech packer:
```
ooo -v out.ooo 
Проверка файла: t1
  Копия 1: ОШИБКА CRC (ожидалось d39177d1, получено bc2c97b5)
  Копия 2: OK
Проверка файла: t2
  Копия 1: OK
```

Extract a file name t1:
```
ooo -x out.ooo ext -f t1
Файл ext/t1 восстановлен из копии 2
```

backup system: sudo find /usr/ -type f -exec ooo -a /root/out.ooo -b 2 '{}' ';'


# Sizing:
```
 odity@viva  ~/bin/pack   main ± dd if=/dev/zero of=file4 bs=1M count=400
 odity@viva  ~/bin/pack   main ± dd if=/dev/zero of=file3 bs=1M count=200
 odity@viva  ~/bin/pack   main ± ./ooo -c out.ooo -b 2 file4            
 odity@viva  ~/bin/pack   main ± ./ooo -c out.ooo -b 3 file3
```
file4=400Mb
file3=200Mb
size = 400 * 2 + 200 * 3 = 1400 Mb
```
 odity@viva  ~/bin/pack   main ± ls -l
-rw-r--r-- 1 odity odity 209715200 мар 19 15:08 file3
-rw-r--r-- 1 odity odity 419430400 мар 19 15:08 file4
-rw-r--r-- 1 odity odity 1468007124 мар 19 15:08 out.ooo

 odity@viva  ~/bin/pack   main ± xz file3
 odity@viva  ~/bin/pack   main ± xz file4
 odity@viva  ~/bin/pack   main ± xz out.ooo
 
 odity@viva  ~/bin/pack   main ± ls -l
-rw-r--r-- 1 odity odity 31556 мар 19 15:08 file3.xz
-rw-r--r-- 1 odity odity 62968 мар 19 15:08 file4.xz
-rw-r--r-- 1 odity odity 220336 мар 19 15:08 out.ooo.xz
```

Result: binary faile save is bad. Text file is good!
