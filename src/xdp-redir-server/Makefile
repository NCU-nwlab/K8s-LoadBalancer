.PONHY: clean

SRCS = $(wildcard *.c)
DEPS = $(SRCS:.c=.d)
KERN_TARGET := xdp_redir_kern.o
USER_TARGET := xdp_redir_user

CLANG = clang
CC = gcc
CFLAGS ?= -g -Wall -O2

all:$(KERN_TARGET) $(USER_TARGET) $(DEPS)

ifneq ("$(wildcard $(DEPS))","")
include $(DEPS)
endif

$(KERN_TARGET):xdp_redir_kern.c
	$(CLANG) -target bpf $(CFLAGS) -c $< -o $@
$(USER_TARGET):xdp_redir_user.o
	$(CC) $< -o $@ -lbpf -lxdp -ljson-c
%.d:%.c
	gcc -MM $^ > $@

clean:
	rm -f $(DEPS)