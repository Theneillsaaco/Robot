#include <WiFiS3.h> 
#include <Servo.h>
#include <Base64.h>

// Configuracion
const char ssid[] = "Isaac";
const char pass[] = "Vladisa182708";

// Pines
const int servoPins[9] = { 0, 2, 3, 4, 5, 6, 7, 8 }; 
float currentPos[9] = { 90, 90, 90, 90, 90, 90, 90, 90, 90 };
int targetPos[9] = { 90, 90, 90, 90, 90, 90, 90, 90, 90 };
unsigned long lastUpdate = 0;

// Parametros
const float STEP_SIZE = 2.0;
const int UPDATE_INTERVAL = 10;

WiFiServer server(80);
WiFiClient wsClient;
bool wsConnected = false;
Servo servo[9];

// Movimiento no bloqueante
void updateServos() {
    if (millis() - lastUpdate < UPDATE_INTERVAL)
        return;
    
    lastUpdate = millis();
    
    for (int i = 1; i <= 8; i++) {
        
        if (i == 7)
            continue;
        
        float diff = targetPos[i] - currentPos[i];
        
        if (abs(diff) > 1.0) {
            // VELOCIDAD ADAPTATIVA: 
            // Si está lejos, se mueve a paso normal. 
            // Si está cerca (menos de 10 grados), reduce la velocidad para no vibrar.
            float speedMult = (abs(diff) < 5) ? 0.5 : 1.0;
            
            if (diff > 0)
                currentPos[i] += STEP_SIZE * speedMult;
            else
                currentPos[i] -= STEP_SIZE * speedMult;
            
            servo[i].write(currentPos[i]);
            
            // Lógica Espejo para el Servo 7
            if (i == 5) {
                currentPos[7] = 180 - currentPos[5];
                servo[7].write((int)currentPos[7]);
            }
        } 
    }
}

// Parsear comando "S:V"
void processCommand(String cmd) {
    cmd.trim();
    int sep = cmd.indexOf(':');
    if (sep == -1) 
        return;
    
    int idx = cmd.substring(0, sep).toInt();
    int val = cmd.substring(sep + 1).toInt();
    
    if (idx < 1 || idx > 6) 
        return;

    targetPos[idx] = constrain(val, 0, 180);
    
    if(idx == 5) {
        targetPos[7] = 180 - targetPos[idx];
    }
    
    Serial.print("Servo "); Serial.print(idx); 
    Serial.print(" moviendo a -> "); Serial.println(val);
}

// WebSocket setup
String extractWSKey(String request) {
    int idx = request.indexOf("Sec-WebSocket-Key: ");
    if (idx == -1) return "";
    int start = idx + 19;
    int end = request.indexOf("\r", start);

    return request.substring(start, end);
}

// SHA-1 + Base64 para el handShake
void sha1(const uint8_t* data, size_t len, uint8_t* digest) {
    uint32_t h0=0x67452301, h1=0xEFCDAB89, h2=0x98BADCFE, h3=0x10325476, h4=0xC3D2E1F0;
    
    // Pre-procesamiento: padding
    size_t newLen = len + 1;
    while (newLen % 64 != 56) 
        newLen++;
    
    uint8_t* msg = (uint8_t*)calloc(newLen + 8, 1);
    memcpy(msg, data, len);
    msg[len] = 0x80;
    
    uint64_t bitLen = (uint64_t)len * 8;
    
    for (int i = 0; i < 8; i++)
        msg[newLen + i] = (bitLen >> (56 - 8 * i)) & 0xFF;

    // Procesar bloques de 512 bits
    for (size_t offset = 0; offset < newLen + 8; offset += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = (uint32_t)msg[offset+i*4] << 24;
            w[i] |= (uint32_t)msg[offset+i*4+1] << 16;
            w[i] |= (uint32_t)msg[offset+i*4+2] << 8;
            w[i] |= (uint32_t)msg[offset+i*4+3];
        }
        for (int i = 16; i < 80; i++) {
            uint32_t t = w[i-3]^w[i-8]^w[i-14]^w[i-16];
            w[i] = (t<<1)|(t>>31);
        }

        uint32_t a=h0, b=h1, c=h2, d=h3, e=h4;
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i<20) {
                f=(b&c)|((~b)&d); 
                k=0x5A827999; 
            } else if (i<40) {
                f=b^c^d; 
                k=0x6ED9EBA1;
            } else if (i<60) {
                f=(b&c)|(b&d)|(c&d); 
                k=0x8F1BBCDC;
            } else {
                f=b^c^d; 
                k=0xCA62C1D6;
            }
            
            uint32_t t = ((a<<5)|(a>>27)) + f + e + k + w[i];
            e=d; d=c; c=(b<<30)|(b>>2); b=a; a=t;
        }
        
        h0+=a; 
        h1+=b; 
        h2+=c; 
        h3+=d; 
        h4+=e;
    }
    
    free(msg);

    // Resultado en bytes
    uint32_t h[5] = {h0,h1,h2,h3,h4};
    for (int i = 0; i < 5; i++) {
        digest[i*4] = (h[i]>>24) &0xFF;
        digest[i*4+1] = (h[i]>>16) &0xFF;
        digest[i*4+2] = (h[i]>>8) &0xFF;
        digest[i*4+3] =  h[i] &0xFF;
    }
}

String computeAcceptKey(String key) {
    String magic = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint8_t digest[20];
    
    sha1((const uint8_t*)magic.c_str(), magic.length(), digest);
    int expectedLen = Base64.encodedLength(20);
    char encoded[expectedLen + 1];
    
    Base64.encode(encoded, (char*)digest, 20);
    encoded[expectedLen] = '\0';
    
    return String(encoded);
}

// Setup
void setup() {
    Serial.begin(115200);
    Serial.print("Firmware WiFi: ");
    Serial.println(WiFi.firmwareVersion());
    
    // Posicion initial y soltar
    for (int i = 1; i <= 8; i++) {
        if (servoPins[i] != 0) {
            servo[i].attach(servoPins[i]);
            servo[i].write(90);
            delay(200); 
        }
    }
    
    WiFi.disconnect();
    delay(1000);
    int result = WiFi.begin(ssid, pass);
    Serial.print("begin result: ");
    Serial.println(result);
    
    // Esperar WL_CONNECTED
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    
    // Esperar IP válida (DHCP puede tardar)
    Serial.println("\nEsperando IP...");
    while (WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
        delay(500);
        Serial.print("~");
    }
    
    Serial.print("\nIP: ");
    Serial.println(WiFi.localIP());
    server.begin();
}

// Leer frame WebSocket
String readWSFrame(WiFiClient& client) {
    if (!client.available()) return "";

    // Esperar header completo
    while (client.available() < 2) return "";

    uint8_t b0 = client.read();
    uint8_t b1 = client.read();

    bool masked = b1 & 0x80;
    uint64_t length = b1 & 0x7F;

    if (length == 126) {
        while (client.available() < 2) return "";
        length  = (uint64_t)client.read() << 8;
        length |= client.read();
    }

    uint8_t mask[4];
    if (masked) {
        while (client.available() < 4) return "";
        for (int i = 0; i < 4; i++)
            mask[i] = client.read();
    }

    // Esperar payload completo
    while (client.available() < length) return "";

    String payload = "";
    for (uint64_t i = 0; i < length; i++) {
        uint8_t byte_ = client.read();
        if (masked)
            byte_ ^= mask[i % 4];
        payload += (char)byte_;
    }

    return payload;
}

// Loop
void loop() {
    updateServos();
    
    if (!wsConnected) {
        WiFiClient newClient = server.available();
        if (newClient) {
            String request = "";
            
            while (newClient.connected() && !request.endsWith("\r\n\r\n")) {
                if (newClient.available()) {
                    request += (char)newClient.read();
                }
            }
            
            if (request.indexOf("Upgrade: websocket") != -1) {
                int start = request.indexOf("Sec-WebSocket-Key: ") + 19;
                String key = request.substring(start, request.indexOf("\r", start));
                newClient.println("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + computeAcceptKey(key) + "\r\n");
                wsClient = newClient;
                wsConnected = true;
                Serial.println("WS Conectado");
            } else {
                newClient.stop();
            }
        }
    } else {
        if (!wsClient.connected()) {
            wsConnected = false;
            Serial.println("WebSocket desconectado.");
        } else if (wsClient.available()) {
            String cmd = readWSFrame(wsClient);
            if (cmd.length() > 0) 
                processCommand(cmd);
        }
    }
}
