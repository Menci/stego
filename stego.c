#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>

typedef struct BitmapHeader {
	uint8_t magic[2]; // { 0x42, 0x4d }
	uint32_t fileSize;
	uint16_t reserved[2]; // { 0, 0 }
	uint32_t dataOffset;
	uint32_t bitmapHeaderSize;
	uint32_t width, height;
	uint16_t planes; // 1
	uint16_t bitCountPerPixel; // 24
	uint32_t compression; // 0
	uint32_t sizeImage;
	uint32_t xPixelsPerMeter, yPixelsPerMeter;
	uint32_t colorsUsed;
	uint32_t colorsImportant;
} __attribute__((packed)) BitmapHeader;

void help(char *argv[]) {
	fprintf(stderr, "Usage: %s -w input_bitmap.bmp input_file output_bitmap.bmp\n", argv[0]);
	fprintf(stderr, "       %s -r input_bitmap.bmp output_file\n", argv[0]);
	exit(2);
}

FILE *openFile(const char *filename, const char *mode, const char *errorMessage) {
	FILE *file = fopen(filename, mode);
	if (!file) {
		perror(errorMessage);
		exit(1);
	}
	return file;
}

size_t align(size_t x, size_t y) {
	return (x + y - 1) / y * y;
}

uint8_t *readBitmap(FILE *file, size_t *dataLength, size_t *availableLength, size_t *rowLength, size_t *rowAvailableLength) {
	static const char *errorMessage = "Failed to read bitmap header";

	BitmapHeader header;
	if (fread(&header, sizeof(BitmapHeader), 1, file) != 1) {
		if (feof(file))
			fprintf(stderr, "%s: unexpected end-of-file\n", errorMessage);
		else
			perror(errorMessage);
		exit(1);
	}

	static const uint8_t magic[2] = { 0x42, 0x4d };
	if (memcmp(header.magic, magic, sizeof(magic)) != 0) {
		fprintf(stderr, "%s: incorrect magic number\n", errorMessage);
		exit(1);
	}

	static const uint16_t reserved[2] = { 0, 0 };
	if (memcmp(header.reserved, reserved, sizeof(reserved)) != 0) {
		fprintf(stderr, "%s: reserved fields must be 0\n", errorMessage);
		exit(1);
	}

	if (header.planes != 1) {
		fprintf(stderr, "%s: unsupported planes value %" PRIu16 "\n", errorMessage, header.planes);
		exit(1);
	}

	if (header.bitCountPerPixel != 24) {
		fprintf(stderr, "%s: unsupported bit count per pixel %" PRIu16 "\n", errorMessage, header.bitCountPerPixel);
		exit(1);
	}

	if (header.compression != 0) {
		fprintf(stderr, "%s: unsupported compression %" PRIu32 "\n", errorMessage, header.compression);
		exit(1);
	}

	if (fseek(file, 0, SEEK_END) == -1) {
		perror(errorMessage);
		exit(1);
	}

	size_t fileSize = ftell(file);
	if (fileSize == -1) {
		perror(errorMessage);
		exit(1);
	}

	size_t bitmapDataRowAvailableLength = (size_t)header.width * 3;
	size_t bitmapDataRowLength = align(bitmapDataRowAvailableLength, 4);
	size_t bitmapDataLength = bitmapDataRowLength * header.height;
	size_t expectedFileSize = header.dataOffset + bitmapDataLength;
	if (expectedFileSize > fileSize) {
		fprintf(stderr, "%s: file expected to be %zu bytes but got %zu bytes only\n", errorMessage, expectedFileSize, fileSize);
		exit(1);
	}

	uint8_t *data = malloc(bitmapDataLength);
	if (!data) {
		perror(errorMessage);
		exit(1);
	}

	if (fseek(file, header.dataOffset, SEEK_SET) == -1) {
		perror(errorMessage);
		exit(1);
	}

	if (fread(data, 1, bitmapDataLength, file) != bitmapDataLength) {
		perror(errorMessage);
		exit(1);
	}

	*dataLength = bitmapDataLength;
	*availableLength = (size_t)header.width * header.height * 3;
	*rowLength = bitmapDataRowLength;
	*rowAvailableLength = bitmapDataRowAvailableLength;
	return data;
}

uint8_t *readFile(FILE *file, size_t *size, const char *errorMessage) {
	if (fseek(file, 0, SEEK_END) == -1) {
		perror(errorMessage);
		exit(1);
	}

	size_t fileSize = ftell(file);
	if (fileSize == -1) {
		perror(errorMessage);
		exit(1);
	}

	if (fseek(file, 0, SEEK_SET) == -1) {
		perror(errorMessage);
		exit(1);
	}

	uint8_t *buffer = malloc(fileSize);
	if (!buffer) {
		perror(errorMessage);
		exit(1);
	}

	if (fread(buffer, 1, fileSize, file) != fileSize) {
		perror(errorMessage);
		exit(1);
	}

	*size = fileSize;
	return buffer;
}

void writeBitmapFileWithNewData(FILE *destnationFile, FILE *sourceFile, uint8_t *newBitmapData, size_t bitmapDataLength) {
	size_t fileLength;
	uint8_t *fileData = readFile(sourceFile, &fileLength, "Failed to read input bitmap file");

	BitmapHeader *header = (BitmapHeader *)fileData;
	memcpy(fileData + header->dataOffset, newBitmapData, bitmapDataLength);

	if (fwrite(fileData, 1, fileLength, destnationFile) != fileLength) {
		perror("Failed to write output bitmap file");
		exit(1);
	}
}

bool getBit(uint8_t x, size_t i) {
	return (x >> i) & 1u;
}

void setBit(uint8_t *x, size_t i, bool bit) {
	if (bit)
		*x = (*x | (1u << i));
	else
		*x = (*x & ~(1u << i));
}

uint8_t *moveToNextAvailableByte(uint8_t *start, uint8_t **current, size_t rowLength, size_t rowAvailableLength) {
	uint8_t *backupCurrent = *current;

	(*current)++;
	size_t i = *current - start;
	size_t rowIndex = i / rowLength;
	if (i == rowIndex * rowLength + rowAvailableLength)
		(*current) += rowLength - rowAvailableLength;

	return backupCurrent;
}

void writeDataToLeastSignificantBitPerRow(uint8_t *destnation, uint8_t *source, size_t destnationRowLength, size_t destnationRowAvailableLength, size_t writeLength) {
	for (uint8_t *pSource = source, *pDestnation = destnation; pSource != source + writeLength; pSource++)
		for (size_t i = 0; i < 8; i++)
			setBit(moveToNextAvailableByte(destnation, &pDestnation, destnationRowLength, destnationRowAvailableLength), 0, getBit(*pSource, i));
}

void readDataFromLeastSignificantBitPerRow(uint8_t *destnation, uint8_t *source, size_t sourceRowLength, size_t sourceRowAvailableLength, size_t readLength) {
	for (uint8_t *pDestnation = destnation, *pSource = source; pDestnation != destnation + readLength; pDestnation++)
		for (size_t i = 0; i < 8; i++)
			setBit(pDestnation, i, getBit(*moveToNextAvailableByte(source, &pSource, sourceRowLength, sourceRowAvailableLength), 0));
}

int main(int argc, char *argv[]) {
	const size_t ARGC_R = 4;
	const size_t ARGC_W = 5;

	bool write;
	if (argc == ARGC_W && strcmp(argv[1], "-w") == 0) write = true;
	else if (argc == ARGC_R && strcmp(argv[1], "-r") == 0) write = false;
	else help(argv);

	FILE *inputBitmapFile = openFile(argv[2], "rb", "Failed to open input bitmap file");

	size_t bitmapDataLength, bitmapAvailableLength, bitmapRowLength, bitmapRowAvailableLength;
	uint8_t *bitmapData = readBitmap(inputBitmapFile, &bitmapDataLength, &bitmapAvailableLength, &bitmapRowLength, &bitmapRowAvailableLength);

	if (write) {
		FILE *inputFile = openFile(argv[3], "rb", "Failed to open input file");

		size_t fileSize;
		uint8_t *fileData = readFile(inputFile, &fileSize, "Failed to read input file");

		size_t bytesToWrite = fileSize + sizeof(uint32_t);
		size_t requiredBytes = bytesToWrite * 8;
		if (requiredBytes > bitmapAvailableLength) {
			fprintf(stderr, "At least %zu bytes of bitmap color data is required but got %zu bytes only.\nPlease use a larger bitmap file.\n", requiredBytes, bitmapDataLength);
			exit(1);
		}

		uint32_t dataLength = fileSize;
		uint32_t bufferSize = sizeof(uint32_t) + dataLength;
		uint8_t *buffer = malloc(bufferSize);
		if (!buffer) {
			perror("Could not allocate memory");
			exit(1);
		}
		memcpy(buffer, &dataLength, sizeof(uint32_t));
		memcpy(buffer + sizeof(uint32_t), fileData, dataLength);

		writeDataToLeastSignificantBitPerRow(bitmapData, buffer, bitmapRowLength, bitmapRowAvailableLength, bufferSize);

		FILE *outputBitmapFile = openFile(argv[4], "wb", "Failed to open output bitmap file");
		writeBitmapFileWithNewData(outputBitmapFile, inputBitmapFile, bitmapData, bitmapDataLength);

		free(buffer);
		free(fileData);
		free(bitmapData);
	} else {
		if (bitmapAvailableLength < sizeof(uint32_t)) {
			fprintf(stderr, "Bitmap too small. This bitmap count not have data.\n");
			exit(1);
		}

		uint32_t dataLength;
		readDataFromLeastSignificantBitPerRow((uint8_t *)&dataLength, bitmapData, bitmapRowLength, bitmapRowAvailableLength, sizeof(uint32_t));

		size_t requiredBytes = (dataLength + sizeof(uint32_t)) * 8;
		if (bitmapAvailableLength < requiredBytes) {
			fprintf(stderr, "Bitmap too small or have no data.\n");
			exit(1);
		}

		uint32_t bufferSize = sizeof(uint32_t) + dataLength;
		uint8_t *buffer = malloc(bufferSize);
		if (!buffer) {
			perror("Could not allocate memory");
			exit(1);
		}

		readDataFromLeastSignificantBitPerRow(buffer, bitmapData, bitmapRowLength, bitmapRowAvailableLength, bufferSize);

		uint8_t *fileData = buffer + sizeof(uint32_t);
		FILE *outputFile = openFile(argv[3], "wb", "Failed to open output file");
		if (fwrite(fileData, 1, dataLength, outputFile) != dataLength) {
			perror("Could not write outptu file");
			exit(1);
		}

		free(buffer);
	}

	return 0;
}
