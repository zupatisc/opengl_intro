##########################################
#           Editable options             #
##########################################

# Compiler options
CXX = clang
CXXFLAGS += -std=c11 -pedantic -Wall -Wno-deprecated-declarations -Os 
LDFLAGS += -pedantic -Wall
LDLIBS += -lGL -lglfw -lGLEW -lGLU
EXECUTABLE_NAME = test

# Folders
SRC = src
BIN = bin
OBJ = $(BIN)/obj

RM = rm -r

# Files
SOURCE_FILES = main.c


##########################################
#    Don't touch anything below this     #
##########################################
EXECUTABLE_FILES = $(EXECUTABLE_NAME:%=$(BIN)/%)
OBJECT_FILES = $(SOURCE_FILES:%.c=$(OBJ)/%.o)


build: $(EXECUTABLE_FILES)

clean: 
	$(RM) $(BIN)

# http://www.gnu.org/software/make/manual/make.html#Phony-Targets
.PHONY: build clean

$(EXECUTABLE_FILES): $(OBJECT_FILES)
	@$(CXX) $(LDLIBS) $(LDFLAGS) -g -o $@ $^
	@# ^^^ http://www.gnu.org/software/make/manual/make.html#Automatic-Variables
	@echo "Build successful!"

# http://www.gnu.org/software/make/manual/make.html#Static-Pattern
$(OBJECT_FILES): $(OBJ)/%.o: $(SRC)/%.c
	@echo Compiling $<
	@# ^^^ Your terminology is weird: you "compile a .cpp file" to create a .o file.
	@mkdir -p $(@D)
	@# ^^^ http://www.gnu.org/software/make/manual/make.html#index-_0024_0028_0040D_0029
	@$(CXX) $(CXXFLAGS) -c -o $@ $<
	@# ^^^ Use $(CFLAGS), not $(LDFLAGS), when compiling.
	
