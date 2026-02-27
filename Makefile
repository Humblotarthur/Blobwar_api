BUILD := build

.PHONY: all clean run

all:
	cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -5
	cmake --build $(BUILD) -j$(shell nproc)

clean:
	rm -rf $(BUILD)

run: all
	./$(BUILD)/blobwar
