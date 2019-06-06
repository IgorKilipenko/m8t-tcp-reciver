#include "libs/ublox.h"

UBLOX_GPS::UBLOX_GPS(UbloxTransport *transport) : SFE_UBLOX_GPS(), _transport{transport} {}
UBLOX_GPS::~UBLOX_GPS() {}

// Checks Serial for data, passing any new bytes to process()
bool UBLOX_GPS::checkUbloxSerial() {
    if (!_transport->available()){
        _transport->pushFromOutStream();
    }
	
	while (_transport->available()) {
		SFE_UBLOX_GPS::process(_transport->read());
	}
	return (true);
}

bool UBLOX_GPS::begin() {
	if (_transport == nullptr) {
		logger.error("Transport is nullptr\n");
		return false;
	}
	return SFE_UBLOX_GPS::begin((Stream&)(*_transport));
}
