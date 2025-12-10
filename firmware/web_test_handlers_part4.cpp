  } else if (type == "json") {
    currentErrorCode = ERR_JSON_PARSE;
    assignmentStatus = 0;
    server.send(200, "text/plain", "[ACTIVE] JSON parse - fast flash");
  } else if (type == "buffer") {
    currentErrorCode = ERR_BUFFER_EXHAUSTED;
    assignmentStatus = 0;
    server.send(200, "text/plain", "[ACTIVE] Buffer full - all solid");
  } else if (type == "green") {
    currentErrorCode = ERR_NONE;
    assignmentStatus = 1;
    server.send(200, "text/plain", "[NORMAL] Green LED");
  } else if (type == "yellow") {
    currentErrorCode = ERR_NONE;
    assignmentStatus = 2;
    server.send(200, "text/plain", "[NORMAL] Yellow LED");
  } else if (type == "red") {
    currentErrorCode = ERR_NONE;
    assignmentStatus = 3;
    server.send(200, "text/plain", "[NORMAL] Red LED");
  } else if (type == "clear") {
    currentErrorCode = ERR_NONE;
    assignmentStatus = 1;
    server.send(200, "text/plain", "[CLEARED] Normal operation");
  } else {
    server.send(400, "text/plain", "Unknown type");
  }
}
