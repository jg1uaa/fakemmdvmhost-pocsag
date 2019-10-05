TARGET = fakemmdvmhost-pocsag
CFLAGS = -g -Wall -O2

all: $(TARGET)

$(TARGET): $(TARGET).c

clean:
	rm -f $(TARGET)
