SDStore::SDStore() {}
SDStore::~SDStore() { end(); }

void SDStore::generateFileName(char *name) {
	bool reset = false;

	size_t number = 1;
	while (number != 0) {
		if (number > _max_nums) {
			number = 1;
			reset = true;
		}
		sprintf(name, "%s_%i.%s", _prefix, number, _ext);
		char path[MAX_PATH_LEN]{0};
		strcat(path, _rootPath);
		strcat(path, name);
		if (SD.exists(path) && !reset) {
			// file exist
			number++;
		} else {
			return;
		}
	}
}

size_t SDStore::getNextFileNumber(const char *dirname) const {

	assert(dirname);

	
	bool reset = false;
	size_t number = 1;

	File root = SD.open(dirname);

	if (!root) {
		logger.debug("Failed to open directory, [%s]", dirname);
		return 0;
	}

	if (!root.isDirectory()) {
		logger.debug("Not a directory, [%s]", dirname);
		return 0;
	}

	File file = root.openNextFile();

	while (file) {
		if (!file.isDirectory()) {
			String fn{file.name()};
			logger.debug("->ITER -> File: [%s]", file.name());
			if (fn.startsWith(_prefix) && fn.endsWith(_ext)) {
				number++;
				logger.debug(" %i\n", number);
				if (number > _max_nums){
					reset = true;
					break;
				}
			}
		}
		file = root.openNextFile();
	}

	if (reset){
		number = 1;
	}

	return number;
}

void SDStore::createFile() {
	generateFileName(filename);

	//size_t num = getNextFileNumber(_rootPath);
	//if (!num){
	//	logger.error("Create file error, next number is 0\n");
	//	return;
	//}

	//sprintf(filename, "%s_%i.%s", _prefix, num, _ext);
	logger.debug("Gen filename: [%s]\n", filename);
	assert(sizeof(_rootPath) + sizeof(filename) < MAX_PATH_LEN);
	
	char path[MAX_PATH_LEN]{0};
	
	strcat(path, _rootPath);
	strcat(path, filename);
	logger.debug("File name: [%s}, dir: [%s}, path: [%s]\n", filename, _rootPath, path);
	if (SD.exists(path)) {
		SD.remove(path);
		logger.debug("File: %s removed\n", filename);
	}

	sdFile = SD.open(path, FILE_WRITE);
	if (sdFile) {
		logger.debug("File: %s created\n", path);
	} else {
		logger.error("Failed to open file for writing, file name : [%s]\n", path);
	}
}

bool SDStore::initSdCard() {
	logger.trace("Start init SD card...\n");
	assert(CS_PIN);
	logger.trace("CS pin : %i\n", CS_PIN);

#ifdef TTGO_BOARD
	SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_SS);
#endif

	if (!SD.begin(CS_PIN /*, SPI_QUARTER_SPEED*/)) {
		// initialization failed!
		logger.debug("initialization failed!.\n");
		_isInitSdCard = false;
	} else {
		logger.debug("initialization done.\n");
		_isInitSdCard = true;
	}

	return _isInitSdCard;
}

void SDStore::end() {
	closeFile();
	if (_isInitSdCard) {
		SD.end();
		_isInitSdCard = false;
		logger.trace("SD card closed, is init = [%s]\n", isInitialize() ? "true" : "false");
	}
}

void SDStore::closeFile() {
	logger.debug("Start closing file\n");
	if (sdFile) {
		//sdFile.flush();
		sdFile.close();
		logger.trace("File closed, isOpenFile = [%s]\n", isOpenFile() ? "true" : "false");
	}
}

size_t SDStore::writeToSD(const char *buffer, size_t bytesCount) { return writeToSD(reinterpret_cast<const uint8_t *>(buffer), bytesCount); }

size_t SDStore::writeToSD(const uint8_t *buffer, size_t bytesCount) {
	logger.trace("Start write to SD card...\n");
	size_t count = sdFile.write(buffer, bytesCount);
	if (count) {
		sdFile.flush();
	}

	logger.trace("Bytes count : [%i]\n", count);
	if (count < bytesCount) {
		if (!count) {
			logger.error("Write failed\n");
		}
		logger.debug("WARN! Not all bytes write to SD, input count = [%i], writed count = [%i]\n", bytesCount, count);
	}
	return count;
}

bool SDStore::isInitialize() const { return _isInitSdCard; }

bool SDStore::isOpenFile() { return sdFile ? true : false; }