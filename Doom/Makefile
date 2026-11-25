GPP = g++
GCC = gcc
LD = ld

LIBHX86_INCLUDE ?= ../../Hashx86-OS/user_prog/libhx86/include
LIBMBEDTLS_INCLUDE ?= ../../Hashx86-OS/user_prog/libmbedtls/include
MBEDTLS_INCLUDE ?= ../../Hashx86-OS/third_party/mbedtls/include
KERNEL_INCLUDE ?= ../../Hashx86-OS/include
LIBHX86_LIB_DIR ?= ../../Hashx86-OS/build/libhx86
LIBMBEDTLS_LIB_DIR ?= ../../Hashx86-OS/build/libmbedtls

INCLUDES = -Iinclude -Iengine -I. -I$(LIBHX86_INCLUDE) -I$(LIBMBEDTLS_INCLUDE) \
           -I$(MBEDTLS_INCLUDE) -I$(KERNEL_INCLUDE)

ENGINE_CXXFLAGS = -m32 -O2 -g -ffreestanding -nostdlib -fno-builtin -fno-rtti \
                  -fno-exceptions -fcommon -fomit-frame-pointer -fpermissive \
                  -Wno-write-strings -Wno-return-type -Wno-narrowing \
                  -Wno-literal-suffix -Wno-register -Wno-enum-compare \
                  -DNORMALUNIX -DLINUX -U__linux__ -Ulinux -U__linux $(INCLUDES)
OUR_CXXFLAGS = $(ENGINE_CXXFLAGS)

ARCHIVE_INCLUDES = -Idata/compat -Iinclude -I$(LIBHX86_INCLUDE) -I$(KERNEL_INCLUDE) \
                   -I../third_party/miniz
ARCHIVE_CFLAGS = -m32 -O2 -g -ffreestanding -nostdlib -fno-builtin -fno-common \
                 -fno-omit-frame-pointer -MMD -MP \
                 -DMINIZ_NO_STDIO -DMINIZ_NO_DEFLATE_APIS -DMINIZ_NO_TIME -DNDEBUG \
                 $(ARCHIVE_INCLUDES)

ENGINE_DIR = engine
OBJ_DIR = ../build/obj/doom
BUILD_DIR = ../build

ENGINE_SRC = $(wildcard $(ENGINE_DIR)/*.c)
ENGINE_OBJS = $(patsubst $(ENGINE_DIR)/%.c,$(OBJ_DIR)/engine/%.o,$(ENGINE_SRC))

OUR_SRC = prog.cpp data/Archive.cpp data/ArchivePort.cpp data/HttpClient.cpp \
          data/StoreUtil.cpp compat.c
OUR_OBJS = $(addprefix $(OBJ_DIR)/, $(OUR_SRC:.cpp=.o))
OUR_OBJS := $(OUR_OBJS:.c=.o)

ARCHIVE_C = \
	../third_party/miniz/miniz.c \
	../third_party/miniz/miniz_tinfl.c \
	../third_party/miniz/miniz_zip.c

ARCHIVE_OBJS = $(addprefix $(OBJ_DIR)/archive/, $(notdir $(ARCHIVE_C:.c=.o)))

ALL_OBJS = $(ENGINE_OBJS) $(OUR_OBJS) $(ARCHIVE_OBJS)
TARGET = $(BUILD_DIR)/Doom.bin

.PHONY: all clean

all: $(TARGET)

$(OBJ_DIR)/engine/%.o: $(ENGINE_DIR)/%.c
	@mkdir -p $(dir $@)
	$(GPP) $(ENGINE_CXXFLAGS) -o $@ -c $<

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(GPP) $(OUR_CXXFLAGS) -Idata/compat -I../third_party/miniz -MMD -MP -o $@ -c $<

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(GPP) $(OUR_CXXFLAGS) -MMD -MP -o $@ -c $<

$(OBJ_DIR)/archive/%.o: ../third_party/miniz/%.c
	@mkdir -p $(dir $@)
	$(GCC) $(ARCHIVE_CFLAGS) -o $@ -c $<

$(TARGET): $(ALL_OBJS) linker.ld
	@mkdir -p $(BUILD_DIR)
	$(LD) -melf_i386 -T linker.ld -o $@ $(ALL_OBJS) \
		-L$(LIBHX86_LIB_DIR) -L$(LIBMBEDTLS_LIB_DIR) -lmbedtls -lhx86 \
		--allow-multiple-definition

-include $(ALL_OBJS:.o=.d)

clean:
	rm -rf $(OBJ_DIR)
	rm -f $(TARGET)
