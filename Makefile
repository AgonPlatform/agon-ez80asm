PROJECTNAME=ez80asm
ARCHITECTURE=linux_elf_x86_64
BUILD_WINDOWS=false
# Tools and arguments
MSBUILD='/mnt/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe' 
MSBUILDFLAGS=/property:Configuration=Release
CC=gcc
LFLAGS=-g -Wall -DUNIX
CFLAGS=$(LFLAGS) -c
OUTFLAG=-o 
RELEASE_LFLAGS=-s -static -Wall -O2 -DNDEBUG -DUNIX -Wno-unused-result
RELEASE_CFLAGS=$(RELEASE_LFLAGS) -c
.DEFAULT_GOAL := linux

# project directories
SRCDIR=src
OBJDIR=obj
BINDIR=bin
LOADERDIR=mosloader
RELEASEDIR=releases
VSPROJECTDIR=$(SRCDIR)/vsproject
VSPROJECTBINDIR=$(VSPROJECTDIR)/x64/Release
# Automatically get all sourcefiles
SRCS=$(wildcard $(SRCDIR)/*.c)
# Automatically get all objects to make from sources
OBJS=$(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRCS))
# Target project binary
BIN=$(BINDIR)/$(PROJECTNAME)

# Default rule
all: linux agon windows

linux: $(BINDIR) $(OBJDIR) $(BIN)

windows:
ifeq ($(BUILD_WINDOWS), true)
	@$(MSBUILD) $(VSPROJECTDIR)/$(PROJECTNAME).sln $(MSBUILDFLAGS)
else
	@echo === Windows build disabled in makefile
endif
agon:
	@echo === Compiling Agon target
	@make --file=Makefile-agon --no-print-directory

# Release with optimal settings for release target
release: CFLAGS=$(RELEASE_CFLAGS)
release: LFLAGS=$(RELEASE_LFLAGS)
release: all
release: $(RELEASEDIR)
release: package

# Linking all compiled objects into final binary
$(BIN):$(OBJS)
	@echo === Linking Linux target
ifeq ($(CC),gcc)
	$(CC) $(LFLAGS) $(OBJS) $(OUTFLAG) $@
else
	$(LINKER) $(LINKERFLAGS)$@ $(OBJS)
endif

# Compile each .c file into .o file
$(OBJDIR)/%.o: $(SRCDIR)/%.c
ifeq ($(CC),gcc)
	$(CC) $(CFLAGS) $< $(OUTFLAG) $@
else
	$(CC) $(CFLAGS)$@ $<
endif

$(BINDIR):
	mkdir $(BINDIR)

$(OBJDIR):
	mkdir $(OBJDIR)

$(RELEASEDIR):
	mkdir $(RELEASEDIR)

package:
	@echo === Packaging binaries
	@tar -zcvf $(RELEASEDIR)/$(PROJECTNAME)_$(ARCHITECTURE).gz $(BINDIR)/$(PROJECTNAME)
	@cp $(BINDIR)/$(PROJECTNAME).bin $(RELEASEDIR)/$(PROJECTNAME).bin
ifeq ($(BUILD_WINDOWS), true)
	@cp $(VSPROJECTBINDIR)/$(PROJECTNAME).exe $(RELEASEDIR)/	
endif
clean:
	@echo Cleaning directories
	@find tests -name "*.output" -type f -delete
ifeq ($(BUILD_WINDOWS), true)
	@$(MSBUILD) $(VSPROJECTDIR)/$(PROJECTNAME).sln $(MSBUILDFLAGS) -t:Clean >/dev/null
endif
ifdef OS
	del /s /q $(BINDIR) >nul 2>&1
	del /s /q $(OBJDIR) >nul 2>&1
	del /s /q $(RELEASEDIR) >nul 2>&1
	del /s /q $(LOADERDIR)/$(PROJECTNAME).bin
else
	@$(RM) -r $(BINDIR) $(OBJDIR) $(RELEASEDIR)
	@$(RM) $(LOADERDIR)/$(PROJECTNAME).bin
endif
