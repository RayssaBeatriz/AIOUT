#include <WiFi.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>


// Configurações do Adafruit IO
#define IO_USERNAME  "insira seu usuário adafruit"
#define IO_KEY       "insira sua adafruit key"
const char* ssid = "NPITI-IoT";
const char* password = "NPITI-IoT";


const char* mqttserver = "io.adafruit.com";
const int mqttport = 1883;
const char* mqttUser = IO_USERNAME;
const char* mqttPassword = IO_KEY;


// Configurações do GitHub
const String repoOwner = "RayssaBeatriz";
const String repoName = "AIOUT";
const String filePath = "STATUS-SENSOR.JSON";  // Mantendo o mesmo arquivo do código original
const String token = "insira seu token github";


// Configurações dos pinos
#define TRIG_PIN 19
#define ECHO_PIN 21
#define LED_PIN 4
#define BUZZER_PIN 27


// Instâncias do Wi-Fi e MQTT
WiFiClient espClient;
PubSubClient client(espClient);


// Variáveis para controle de distância e tempo
const int distanciaLimite = 10;
unsigned long inicioDeteccao = 0;
unsigned long ultimoEstadoGitHub = 0;
const unsigned long intervaloGitHub = 5000;
bool ledLigado = false;
bool estadoAnteriorSensor = false;


// Função para conectar ao Wi-Fi
void setup_wifi() {
    Serial.println("Conectando ao WiFi...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print("...");
    }
    Serial.println("WiFi conectado");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
}


// Função para reconectar ao MQTT
void reconnect() {
    if (client.connected()) {
        return;
    }


    Serial.println("Tentando reconectar ao MQTT...");
   
    while (!client.connected()) {
        String clientId = "ESP32-Client";
        clientId += String(random(0xffff), HEX);
       
        if (client.connect(clientId.c_str(), mqttUser, mqttPassword)) {
            Serial.println("Conectado ao MQTT!");
            return;
        } else {
            Serial.print("Falha na conexão, rc=");
            Serial.println(client.state());
            delay(5000);
        }
    }
}


// Função para medir a distância usando o sensor ultrassônico
float medirDistancia() {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long duracao = pulseIn(ECHO_PIN, HIGH);
    return (duracao / 2.0) * 0.0343;
}


// Função para codificar em Base64
String base64_encode(const String &input) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String encoded = "";
    int padding = 0;
    int val = 0;
    int valb = -6;


    for (unsigned int i = 0; i < input.length(); i++) {
        val = (val << 8) + input[i];
        valb += 8;
        while (valb >= 0) {
            encoded += table[(val >> valb) & 0x3F];
            valb -= 6;
        }
    }


    if (valb > -6) {
        encoded += table[((val << 8) >> (valb + 8)) & 0x3F];
    }


    while (encoded.length() % 4) {
        encoded += "=";
        padding++;
    }


    return encoded;
}


// Função para atualizar o estado no GitHub com SHA
void updateGitHub(bool sensorStatus) {
    if (millis() - ultimoEstadoGitHub < intervaloGitHub && sensorStatus == estadoAnteriorSensor) {
        return;
    }


    HTTPClient http;
    String url = "https://api.github.com/repos/" + repoOwner + "/" + repoName + "/contents/" + filePath;
    http.begin(url);
    http.addHeader("Authorization", "token " + token);
    http.addHeader("Accept", "application/vnd.github.v3+json");


    int httpCode = http.GET();
    if (httpCode > 0) {
        String payload = http.getString();
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, payload);
        String sha = doc["sha"];


        String content = "{\"sensor2\": " + String(sensorStatus ? "true" : "false") + "}";
        String encodedContent = base64_encode(content);
        String jsonData = "{\"message\": \"update sensor 2 status\", \"content\": \"" + encodedContent + "\", \"sha\": \"" + sha + "\"}";


        http.begin(url);
        http.addHeader("Authorization", "token " + token);
        http.addHeader("Accept", "application/vnd.github.v3+json");


        httpCode = http.PUT(jsonData);
        if (httpCode > 0) {
            Serial.println("Estado atualizado no GitHub.");
        } else {
            Serial.print("Erro ao atualizar estado no GitHub. Código HTTP: ");
            Serial.println(httpCode);
        }
    } else {
        Serial.print("Erro ao obter SHA do arquivo. Código HTTP: ");
        Serial.println(httpCode);
    }
    http.end();


    ultimoEstadoGitHub = millis();
    estadoAnteriorSensor = sensorStatus;
}


void setup() {
    Serial.begin(115200);
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(LED_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    setup_wifi();
    client.setServer(mqttserver, mqttport);
}


void loop() {
    if (!client.connected()) {
        reconnect();
    }
    client.loop();


    float distancia = medirDistancia();
    unsigned long tempoAtual = millis();


    if (distancia > 0 && distancia < distanciaLimite) {
        if (!ledLigado && tempoAtual - inicioDeteccao >= 1000) {
            ledLigado = true;
            digitalWrite(LED_PIN, HIGH);  // Acende o LED
            Serial.println("Objeto detectado, LED ligado!");
            client.publish("Pedrin/feeds/condicionador", "1");
            updateGitHub(true);
        }
    } else {
        inicioDeteccao = tempoAtual;
        if (ledLigado) {
            ledLigado = false;
            digitalWrite(LED_PIN, LOW);  // Apaga o LED quando a detecção terminar
            Serial.println("Detecção terminada, LED apagado!");
            client.publish("Pedrin/feeds/condicionador", "0");
            updateGitHub(false);
        }
    }
}








