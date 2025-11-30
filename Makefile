PROJECTNAME=ez80asm
# Environment
OS_NAME = $(shell uname -s | tr A-Z a-z)
ARCH = $(shell uname -m | tr A-Z a-z)
# Tools and arguments
MSBUILD='/mnt/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe' 
MSBUILDFLAGS=/property:Configuration=Release
CC=gcc
LFLAGS=-g -Wall -DUNIX
CFLAGS=$(LFLAGS) -c -fno-common -static -Wall -O2 -DNDEBUG -Wno-unused-result -c
OUTFLAG=-o 
.DEFAULT_GOAL := linux

# project directories
SRCDIR=src
OBJDIR=obj
BINDIR=bin
LOADERDIR=mosloader
RELEASEDIR=release
VSPROJECTDIR=$(SRCDIR)/vsproject
VSPROJECTBINDIR=$(VSPROJECTDIR)/x64/Release
# Automatically get all sourcefiles
SRCS=$(wildcard $(SRCDIR)/*.c)
# Automatically get all objects to make from sources
OBJS=$(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRCS))
# Target project binary
BIN=$(BINDIR)/$(PROJECTNAME)

linux: $(BINDIR) $(OBJDIR) $(BIN) $(RELEASEDIR)
	@echo === Creating release binary
	@tar -zcvf $(RELEASEDIR)/$(PROJECTNAME)-$(OS_NAME)_$(ARCH).tar.gz $(BINDIR)/$(PROJECTNAME) 2>/dev/null

windows: $(RELEASEDIR)
	@$(MSBUILD) $(VSPROJECTDIR)/$(PROJECTNAME).sln $(MSBUILDFLAGS)
	@echo === Creating release binary
	@cp $(VSPROJECTBINDIR)/$(PROJECTNAME).exe $(RELEASEDIR)/	

agon: $(RELEASEDIR)
	@echo === Compiling Agon target
	@make --file=Makefile-agon --no-print-directory

	@echo === Creating release binary
	@cp $(BINDIR)/$(PROJECTNAME).bin $(RELEASEDIR)/$(PROJECTNAME).bin

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
	@mkdir $(BINDIR)

$(OBJDIR):
	@mkdir $(OBJDIR)

$(RELEASEDIR):
	@mkdir $(RELEASEDIR)

clean:
	@echo Cleaning directories
	@find tests -name "*.output" -type f -delete
ifeq ($@,windows)
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
