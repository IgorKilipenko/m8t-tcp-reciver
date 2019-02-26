bool AWebServer::_static_init = false;

const char *AWebServer::API_P_GPSCMD = "cmd";

AWebServer::AWebServer(ATcpServer *telnetServer) : ssid{APSSID}, password{APPSK}, server{80}, ws{"/ws"}, events{"/events"}, telnetServer{telnetServer}, api{} {}

AWebServer::~AWebServer() {
	if (telnetServer) {
		delete telnetServer;
		telnetServer = nullptr;
	}
}

void AWebServer::setup() {
	loadWiFiCredentials();

	WiFi.hostname(hostName);
	WiFi.mode(WIFI_AP_STA);
	WiFi.softAP("ESPPP");

	if (ssid && password) {
		WiFi.begin(ssid, password);
		while (WiFi.waitForConnectResult() != WL_CONNECTED) {
			logger.printf("STA: Failed!\n");
			WiFi.disconnect(false);
			delay(1000);
			WiFi.begin(ssid, password);
		}
	}

	// Send OTA events to the browser
	ArduinoOTA.onStart([&]() { events.send("Update Start", "ota"); });
	ArduinoOTA.onEnd([&]() { events.send("Update End", "ota"); });
	ArduinoOTA.onProgress([&](unsigned int progress, unsigned int total) {
		char p[32];
		sprintf(p, "Progress: %u%%\n", (progress / (total / 100)));
		events.send(p, "ota");
	});
	ArduinoOTA.onError([&](ota_error_t error) {
		if (error == OTA_AUTH_ERROR)
			events.send("Auth Failed", "ota");
		else if (error == OTA_BEGIN_ERROR)
			events.send("Begin Failed", "ota");
		else if (error == OTA_CONNECT_ERROR)
			events.send("Connect Failed", "ota");
		else if (error == OTA_RECEIVE_ERROR)
			events.send("Recieve Failed", "ota");
		else if (error == OTA_END_ERROR)
			events.send("End Failed", "ota");
	});
	ArduinoOTA.setHostname(hostName);
	ArduinoOTA.begin();

	MDNS.addService("http", "tcp", 80);

	SPIFFS.begin();

	ws.onEvent([&](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) { onWsEvent(server, client, type, arg, data, len); });

	server.addHandler(&ws);

	events.onConnect([&](AsyncEventSourceClient *client) { client->send("hello!", NULL, millis(), 1000); });

	server.addHandler(&events);

	server.addHandler(new SPIFFSEditor(http_username, http_password));

	server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(200, "text/plain", String(ESP.getFreeHeap())); });

	server.serveStatic("/", SPIFFS, "/www/").setDefaultFile("index.html");

	server.onNotFound([&](AsyncWebServerRequest *request) {
		logger.printf("NOT_FOUND: ");
		if (request->method() == HTTP_GET)
			logger.printf("GET");
		else if (request->method() == HTTP_POST)
			logger.printf("POST");
		else if (request->method() == HTTP_DELETE)
			logger.printf("DELETE");
		else if (request->method() == HTTP_PUT)
			logger.printf("PUT");
		else if (request->method() == HTTP_PATCH)
			logger.printf("PATCH");
		else if (request->method() == HTTP_HEAD)
			logger.printf("HEAD");
		else if (request->method() == HTTP_OPTIONS) {
			request->send(200);
			return;
		} else
			logger.printf("UNKNOWN");
		logger.printf(" http://%s%s\n", request->host().c_str(), request->url().c_str());

		if (request->contentLength()) {
			logger.printf("_CONTENT_TYPE: %s\n", request->contentType().c_str());
			logger.printf("_CONTENT_LENGTH: %u\n", request->contentLength());
		}

		int headers = request->headers();
		int i;
		for (i = 0; i < headers; i++) {
			AsyncWebHeader *h = request->getHeader(i);
			logger.printf("_HEADER[%s]: %s\n", h->name().c_str(), h->value().c_str());
		}

		int params = request->params();
		for (i = 0; i < params; i++) {
			AsyncWebParameter *p = request->getParam(i);
			if (p->isFile()) {
				logger.printf("_FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
			} else if (p->isPost()) {
				logger.printf("_POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
			} else {
				logger.printf("_GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
			}
		}

		request->send(404);
	});

	server.onFileUpload([&](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
		if (!index)
			logger.printf("UploadStart: %s\n", filename.c_str());
		logger.printf("%s", (const char *)data);
		if (final)
			logger.printf("UploadEnd: %s (%u)\n", filename.c_str(), index + len);
	});
	server.onRequestBody([&](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
		if (!index)
			logger.printf("BodyStart: %u\n", total);
		logger.printf("%s", (const char *)data);
		if (index + len == total)
			logger.printf("BodyEnd: %u\n", total);
	});

	/* API  ================================ */
	server.on("/api/wifi/scan", HTTP_GET, [&](AsyncWebServerRequest *request) {
		logger.debug("WIFI Scan request\n");

		String json = "[";
		int n = WiFi.scanComplete();
		if (n == -2) {
			WiFi.scanNetworks(true);
		} else if (n) {
			for (int i = 0; i < n; ++i) {
				if (i)
					json += ",";
				json += "{";
				json += "\"rssi\":" + String(WiFi.RSSI(i));
				json += ",\"ssid\":\"" + WiFi.SSID(i) + "\"";
				json += ",\"bssid\":\"" + WiFi.BSSIDstr(i) + "\"";
				json += ",\"channel\":" + String(WiFi.channel(i));
				json += ",\"secure\":" + String(WiFi.encryptionType(i));
				json += ",\"hidden\":" + String(WiFi.isHidden(i) ? "true" : "false");
				json += "}";
			}
			WiFi.scanDelete();
			if (WiFi.scanComplete() == -2) {
				WiFi.scanNetworks(true);
			}
		}
		json += "]";

		AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
		// response->addHeader("Access-Control-Expose-Headers","Access-Control-Allow-Origin");
		// response->addHeader("Access-Control-Allow-Origin","*");
		// response->addHeader("Content-Encoding", "utf8");
		request->send(response);
		json = String();
	});

	server.on("/api/gnss/cmd", HTTP_POST, [&](AsyncWebServerRequest *request) {
		logger.debug("GPS start/stop request\n");
		logger.debug("Argument count %i\n", request->args());
		if (request->args() == 0 || !request->hasArg(API_P_GPSCMD)) {
			request->send(404);
			return;
		}
		logger.debug("Server has gps atr %s\n", request->hasArg(API_P_GPSCMD) ? "true" : "false");

		unsigned long stop = 0;
		unsigned long start = 0;
		if (request->arg(API_P_GPSCMD) == "1" && !telnetServer->isInProgress()) {
			telnetServer->startReceive();
			start = millis();
		} else if (telnetServer->isInProgress()) {
			telnetServer->stopReceive();
			stop = millis();
		}
		String json = "{\"enabled\":";
		if (telnetServer->isInProgress()) {
			json += "true";
			if (start) {
				json += ",\"startTime\":";
				json += start;
			}

		} else {
			json += "false";
			if (stop) {
				json += ",\"stopTime\":";
				json += stop;
			}
		}
		json += "}";
		AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
		request->send(response);
		json = String();
	});
	server.on("/api/server/info", HTTP_GET, [&](AsyncWebServerRequest *request) {
		logger.debug("Server info request\n");

		String json = "{";
		json += "\"enabled\":" + String(telnetServer->isInProgress() ? "true" : "false");
		json += ",\"serverTime\":";
		json += millis();
		json += "}";
		AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
		request->send(response);
		json = String();
	});

	server.on("/tests/ansi", HTTP_GET, [&](AsyncWebServerRequest *request) {
		String text = "Тест ansi";
		AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", text);
		request->send(response);
		text = String();
	});

	server.onRequestBody([&](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
		logger.debug("\n onRequestBody \n");
		if (!index) {
			logger.printf("BodyStart: %u B\n", total);
		}
		for (size_t i = 0; i < len; i++) {
			logger.print(data[i]);
		}
		if (index + len == total) {
			logger.printf("BodyEnd: %u B\n", total);
		}
	});
#ifdef REST_API
	AsyncCallbackJsonWebHandler *handler = new AsyncCallbackJsonWebHandler("/rest/endpoint", [](AsyncWebServerRequest *request, JsonVariant &json) {
		logger.debug("Old api \n");
		if (!json) {
			logger.debug("Json is empty\n");
			return;
		}
		JsonObject &jsonObj = json.as<JsonObject>();
		if (jsonObj.success()) {
			logger.debug("Json parse success\n");
			if (jsonObj.containsKey("rest_test")) {
				logger.debug("The REST API test SUCCESS\n");
				// logger.debug("{ \"%s\" : %s }", jsonObj["rest_test"].as<const char*>() | "err");
				jsonObj.prettyPrintTo(logger);
			}
		} else {
			logger.debug("Json parsing failed\n");
		}

		AsyncJsonResponse *response = new AsyncJsonResponse();
		response->addHeader("Server", "ESP Async Web Server");
		JsonObject &root = response->getRoot();
		root["heap"] = ESP.getFreeHeap();
		response->setLength();
		request->send(response);
	});
	server.addHandler(handler);

	api.on("query", "receiver", [&](JsonObject &res){
		logger.debug("API parsing success\n");
		res.prettyPrintTo(logger);
	});
	AsyncCallbackJsonWebHandler *apiHandler = new AsyncCallbackJsonWebHandler("/api", [&](AsyncWebServerRequest *request, JsonVariant &json) {
		logger.debug("API \n");
		if (!json) {
			logger.debug("Json is empty\n");
			return;
		}
		JsonObject &jsonObj = json.as<JsonObject>();
		if (jsonObj.success()) {
			logger.debug("Json parse success\n");
			api.parse(jsonObj);

		} else {
			logger.debug("Json parsing failed\n");
		}

		AsyncJsonResponse *response = new AsyncJsonResponse();
		response->addHeader("Server", "ESP Async Web Server");
		JsonObject &root = response->getRoot();
		root["heap"] = ESP.getFreeHeap();
		response->setLength();
		request->send(response);
	});
	server.addHandler(apiHandler);
#endif // REST_API

	telnetServer->setup();

	initDefaultHeaders();
	server.begin();
}

void AWebServer::onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
	if (type == WS_EVT_CONNECT) {
		logger.printf("ws[%s][%u] connect\n", server->url(), client->id());
		client->printf("WS started, client id: %u :)", client->id());
		client->ping();
	} else if (type == WS_EVT_DISCONNECT) {
		logger.printf("ws[%s] disconnect: %u\n", server->url(), client->id());
	} else if (type == WS_EVT_ERROR) {
		logger.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t *)arg), (char *)data);
	} else if (type == WS_EVT_PONG) {
		logger.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len) ? (char *)data : "");
	} else if (type == WS_EVT_DATA) {
		AwsFrameInfo *info = (AwsFrameInfo *)arg;
		String msg = "";
		if (info->final && info->index == 0 && info->len == len) {
			// the whole message is in a single frame and we got all of it's data
			logger.printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT) ? "text" : "binary", info->len);

			if (info->opcode == WS_TEXT) {
				for (size_t i = 0; i < info->len; i++) {
					msg += (char)data[i];
				}
			} else {
				char buff[3];
				for (size_t i = 0; i < info->len; i++) {
					sprintf(buff, "%02x ", (uint8_t)data[i]);
					msg += buff;
				}
			}
			logger.printf("%s\n", msg.c_str());

			if (info->opcode == WS_TEXT)
				client->text("I got your text message");
			else
				client->binary("I got your binary message");
		} else {
			// message is comprised of multiple frames or the frame is split into multiple packets
			if (info->index == 0) {
				if (info->num == 0)
					logger.printf("ws[%s][%u] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT) ? "text" : "binary");
				logger.printf("ws[%s][%u] frame[%u] start[%llu]\n", server->url(), client->id(), info->num, info->len);
			}

			logger.printf("ws[%s][%u] frame[%u] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT) ? "text" : "binary", info->index, info->index + len);

			if (info->opcode == WS_TEXT) {
				for (size_t i = 0; i < info->len; i++) {
					msg += (char)data[i];
				}
			} else {
				char buff[3];
				for (size_t i = 0; i < info->len; i++) {
					sprintf(buff, "%02x ", (uint8_t)data[i]);
					msg += buff;
				}
			}
			logger.printf("%s\n", msg.c_str());

			if ((info->index + len) == info->len) {
				logger.printf("ws[%s][%u] frame[%u] end[%llu]\n", server->url(), client->id(), info->num, info->len);
				if (info->final) {
					logger.printf("ws[%s][%u] %s-message end\n", server->url(), client->id(), (info->message_opcode == WS_TEXT) ? "text" : "binary");
					if (info->message_opcode == WS_TEXT)
						client->text("I got your text message");
					else
						client->binary("I got your binary message");
				}
			}
		}
	}
}

void AWebServer::process() { ArduinoOTA.handle(); }

/** Load WLAN credentials from EEPROM */
void AWebServer::loadWiFiCredentials() {
	logger.debug("ssid sizeof: ");
	logger.debug("%i\n", sizeof(ssid));
	logger.debug("EEROM start\n");
	EEPROM.begin(512);
	logger.debug("EEROM begin\n");
	EEPROM.get(0, ssid);
	EEPROM.get(0 + sizeof(ssid), password);
	char ok[2 + 1];
	EEPROM.get(0 + sizeof(ssid) + sizeof(password), ok);
	EEPROM.end();
	if (String(ok) != String("OK")) {
		ssid[0] = 0;
		password[0] = 0;
	}
	logger.println("Recovered credentials:");
	logger.println(ssid);
	logger.println(strlen(password) > 0 ? "********" : "<no password>");
}

/** Store WLAN credentials to EEPROM */
void AWebServer::saveWiFiCredentials() {
	EEPROM.begin(512);
	EEPROM.put(0, ssid);
	EEPROM.put(0 + sizeof(ssid), password);
	char ok[2 + 1] = "OK";
	EEPROM.put(0 + sizeof(ssid) + sizeof(password), ok);
	EEPROM.commit();
	EEPROM.end();
}

void AWebServer::initDefaultHeaders() {
	if (!AWebServer::_static_init) {
		DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
		// DefaultHeaders::Instance().addHeader("Access-Control-Expose-Headers","Access-Control-Allow-Origin");
		DefaultHeaders::Instance().addHeader("charset", "ANSI");
		DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET,HEAD,OPTIONS,POST,PUT");
		DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers",
											 "Access-Control-Allow-Headers, Origin,Accept, X-Requested-With, Content-Type, Access-Control-Request-Method, Access-Control-Request-Headers");
		AWebServer::_static_init = true;
	}
}