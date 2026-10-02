# OTYS_HW3

## Состав репозитория

```
.
├── src/runtime.c      # исходник
└── README.md          # отчёт
```

## Сборка

```bash
sudo apt install -y gcc
gcc -Wall -o runtime runtime.c
```

## Подготовка rootfs

```bash
mkdir -p /tmp/alpine && cd /tmp/alpine
curl -O https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/x86_64/alpine-minirootfs-3.20.3-x86_64.tar.gz
tar -xzf alpine-minirootfs-3.20.3-x86_64.tar.gz
rm alpine-minirootfs-*.tar.gz
```

## Запуск

```bash
./runtime run --rootfs /tmp/alpine /bin/ls
./runtime run --rootfs /tmp/alpine /bin/ps
./runtime run --rootfs /tmp/alpine /bin/sh -c 'id; hostname'
```

---

## 1. Проверка rootfs 

```bash
runtime run --rootfs /tmp/alpine /bin/ls
```

**Хост:**
```bash
$ ls /
bin   cdrom  etc   lib    lib64   lost+found  mnt  proc  run   snap  swapfile  tftpboot  usr
boot  dev    home  lib32  libx32  media       opt  root  sbin  srv   sys       tmp       var
```

**Контейнер:**
```bash
$ ./runtime run --rootfs /tmp/alpine /bin/ls
bin  dev  etc  home  lib  media  mnt  opt  proc  root  run  sbin  srv  sys  tmp  usr  var
```

Выводы различаются → `chroot()` работает, контейнер видит файлы **внутри rootfs**, а не хостовой системы.

---

## 2. Проверка PID namespace 
bash```
runtime run --rootfs /tmp/alpine /bin/ps
```

```bash
$ ./runtime run --rootfs /tmp/alpine /bin/ps
PID   USER     TIME  COMMAND
    1 root      0:00 /bin/ps
```

Процесс видит себя под **PID 1** → `CLONE_NEWPID` работает. Для наглядности `ps aux | head -5` на хосте:

```
USER         PID %CPU %MEM    VSZ   RSS TTY      STAT START   TIME COMMAND
root           1  0.0  0.0 168408 11892 ?        Ss   14:10   0:01 /sbin/init splash
root           2  0.0  0.0      0     0 ?        S    14:10   0:00 [kthreadd]
root           3  0.0  0.0      0     0 ?        I<   14:10   0:00 [rcu_gp]
root           4  0.0  0.0      0     0 ?        I<   14:10   0:00 [rcu_par_gp]
```

Ни одного из этих процессов контейнер не видит - pid-namespace полностью изолирован.

---

## 3. Данные о пользователе и хосте

**Хост (родительский процесс):**
```bash
$ id
uid=1000(xilinx) gid=1000(xilinx) groups=1000(xilinx),4(adm),20(dialout),24(cdrom),27(sudo),30(dip),46(plugdev),120(lpadmin),131(lxd),132(sambashare),998(vboxsf)

$ hostname
xilinx
```

**Контейнер:**
```bash
$ ./runtime run --rootfs /tmp/alpine /bin/sh -c 'id; hostname'
uid=0(root) gid=0(root) groups=65534(nobody),65534(nobody),65534(nobody),65534(nobody),65534(nobody),65534(nobody),65534(nobody),65534(nobody),65534(nobody),65534(nobody),0(root)
container
```

### Сводная таблица

| Параметр  | Хост                                          | Контейнер                        |
|-----------|-----------------------------------------------|----------------------------------|
| uid       | `1000(xilinx)`                                | `0(root)`                        |
| gid       | `1000(xilinx)`                                | `0(root)`                        |
| hostname  | `xilinx`                                      | `container`                      |
| groups    | `1000(xilinx), 4(adm), 20(dialout), …`        | `65534(nobody)` × 10 + `0(root)` |

### Пояснения

- **uid 0 / gid 0 в контейнере.** Это результат записи в
  `/proc/<pid>/uid_map` и `/proc/<pid>/gid_map`:
  ```
  0 1000 1
  ```
  Наш непривилегированный `1000(xilinx)` отображён на `0(root)` **внутри user namespace**.
  Снаружи, на хосте, процесс по-прежнему работает от uid 1000 - реального root
  нет. Именно это делает решение **полностью rootless**.

- **hostname = `container`** - задан через `sethostname()` внутри изолированного
  UTS-namespace. На хосте `hostname` остался `xilinx` - namespace изолирован.

---

## 4. Проверка network namespace

```bash
$ ./runtime run --rootfs /tmp/alpine /bin/sh -c 'cat /proc/net/dev'
Inter-|   Receive                                                |  Transmit
 face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed
    lo:       0       0    0    0    0     0          0         0        0       0    0    0    0     0       0          0
```

Внутри контейнера виден **только `lo`**. Хостовых `eth0`/`wlan0`
нет, отсюда следует, что `CLONE_NEWNET` работает.

---

**Вывод.** Без `CLONE_NEWNS` mount("proc", ...) либо вернёт EPERM, либо перезапишет /proc хоста, сломав ps, top и systemd. Ядро привязывает /proc к pid-namespace на момент монтирования, поэтому при изолированном pid-ns и неизолированном mount-ns разные процессы увидят разные версии /proc — получится путаница. Именно поэтому CLONE_NEWNS обязателен: он даёт контейнеру отдельную копию таблицы монтирований, не затрагивая хост.


---

