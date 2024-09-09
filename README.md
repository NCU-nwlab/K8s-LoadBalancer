# K8s-LoadBalancer

# Dependencies
## Git Submodules
This repository uses [libbpf](https://github.com/libbpf/libbpf/), [libxdp](https://github.com/xdp-project/xdp-tools/), and [json-c](https://github.com/json-c/json-c/) as git-submodules
You can initialze submodules when cloning this repo:
```
git clone --recurse-submodules git@github.com:KerstinHung/xdp-redir-server.git
```
If you have already cloned the repository, you can initialize the submodule by running the following command:
```
git submodule update --init
```
## Install Packages
### Packages on Fedora
```
sudo dnf install clang llvm cmake -y
sudo dnf install libbpf-devel elfutils-libelf-devel libpcap-devel perf glibc-devel.i686 m4 pkg-config -y
```
Fedora by default sets a limit on the amount of locked memory the kernel will allow, which can interfere with loading BPF maps. Use this command to raise the limit:
```
ulimit -l 1024
```
## Build Submodules
Before compiling libxdp, you need to install libbpf on your system. See more on [libbpf's](https://github.com/libbpf/libbpf) readme.
Assume you are in the root directory of your workspace and already added libbpf as a submodule:
```
cd libbpf/src/
make
```
Then go back to the root directory of your workspace, you can build xdp-tools:
```
cd xdp-tools/
./configure
make libxdp
```
Finally, install the binaries on local machine
```
sudo make install
```
Since the `libxdp.so` shared object file will be installed in `/usr/local/lib`, where ldconfig doesn't know.
It is recommended that you add this path in `/etc/ld.so.conf` first:
```
sudo vim /etc/ld.so.conf
```
add the path to the end of the file:
 ```
/usr/local/lib
```
After saving and leaving the vim editor, update your changes:
 ```
sudo ldconfig -v
```
As for building json-c, just follow the readme of [json-c](https://github.com/json-c/json-c/?tab=readme-ov-file#buildunix)
Or you can follow the instructions below (As always, back to the root directory of your workspace first):
```
cd json-c/
mkdir json-c-build
cd json-c-build
cmake ../
make
make test
sudo make install
```
Note: We do not need their optional packages.
# Loading XDP Program
Compile the kernel space file in the src directory:
```
cd src/xdp-redir-server
make
```

Execute the user space program with interface name. For example, if this machine is for load balancer:
```
sudo ./xdp_redir_user --dev lo --progname xdp_redir_func
```

In Fedora, you can check the loading state with this command:
```
sudo ./xdp-loader status lo
```

After loading your xdp program into the kernel, you can remove the dependency file:
```
make clean
```