# ooo
This is a file packer with replica support. You add any files to the archive and assign the number of replicas. Each replica calculates the checksum, file rights and size. You can view the contents of the archive or delete a file from the archive. The most important thing.... If your disk sector is broken and after restoring the file system the sector has changed its contents, then when extracting the file from the archive, the checksum of the replica is checked and if it is broken, the file is extracted from the next replica.

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
