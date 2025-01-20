#include <WiFi.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>


// Configurações do Adafruit IO
#define IO_USERNAME  "Pedrin"
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
const String filePath = "STATUS-SENSOR.JSON";
const String token = "insira seu token github";


// Configurações dos pinos
#define TRIG_PIN 19
#define ECHO_PIN 21
#define LED_PIN 4
#define DHT_PIN 15
#define DHT_TYPE DHT22
#define BUZZER_PIN 27


// Instância do sensor DHT
DHT dht(DHT_PIN, DHT_TYPE);


// Instâncias do Wi-Fi e MQTT
WiFiClient espClient;
PubSubClient client(espClient);


// Variáveis para controle de distância e tempo
const int distanciaLimite = 10;
const unsigned long tempoDeteccao = 6000; // 6 segundos
unsigned long inicioDeteccao = 0;
bool ledLigado = false;
unsigned long tempoUltimoPiscar = 0;
const unsigned long intervaloPiscar = 1000; // 1 segundo para piscar
unsigned long tempoUltimaAlteracao = 0;  // Variável para controle de tempo sem delay


// Funções SPIFFS
void salvarEstadoLed(bool estado) {
    File file = SPIFFS.open("/estado_led.txt", FILE_WRITE);
    if (!file) {
        Serial.println("Erro ao abrir o arquivo para escrita");
        return;
    }
    file.print(estado ? "1" : "0");
    file.close();
    Serial.print("Estado do LED salvo no SPIFFS: ");
    Serial.println(estado ? "LIGADO" : "DESLIGADO");
}


bool lerEstadoLed() {
    File file = SPIFFS.open("/estado_led.txt", FILE_READ);
    if (!file) {
        Serial.println("Erro ao abrir o arquivo para leitura. Definindo estado padrão: DESLIGADO");
        return false; // Padrão: LED desligado
    }
    String estado = file.readString();
    file.close();
    Serial.print("Estado do LED lido do SPIFFS: ");
    Serial.println(estado == "1" ? "LIGADO" : "DESLIGADO");
    return estado == "1";
}


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


// Função para reconectar ao MQTT com limite de tentativas
void reconnect() {
    static int tentativa = 0;
    if (client.connected()) {
        return;
    }


    Serial.println("Tentando reconectar ao MQTT...");
   
    while (!client.connected()) {
        String clientId = "ESP32-Client";
        clientId += String(random(0xffff), HEX);
       
        // Tenta conectar com o broker MQTT
        if (client.connect(clientId.c_str(), mqttUser, mqttPassword)) {
            Serial.println("Conectado ao MQTT!");
            tentativa = 0;  // Resetando tentativas
            return;
        } else {
            // Se a conexão falhar, aumenta o contador de tentativas
            Serial.print("Falha na conexão, rc=");
            Serial.println(client.state());
            tentativa++;
            if (tentativa < 5) {
                Serial.println("Tentando novamente em 5 segundos...");
                delay(5000);
            } else {
                Serial.println("Máximo de tentativas atingido. Aguardando 30 segundos para tentar novamente.");
                delay(30000); // Atraso maior após múltiplas tentativas
            }
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


// Função para decodificar o conteúdo Base64
String decodificarBase64(String base64Content) {
    String decodedString = "";
    int len = base64Content.length();
    byte decodedData[(len * 3) / 4]; // Estimativa de tamanho decodificado


    // Tabela de decodificação Base64
    const char* base64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
   
    // Loop para decodificar manualmente Base64
    int index = 0;
    for (int i = 0; i < len; i += 4) {
        byte byte1 = strchr(base64chars, base64Content[i]) - base64chars;
        byte byte2 = strchr(base64chars, base64Content[i + 1]) - base64chars;
        byte byte3 = strchr(base64chars, base64Content[i + 2]) - base64chars;
        byte byte4 = strchr(base64chars, base64Content[i + 3]) - base64chars;


        decodedData[index++] = (byte1 << 2) | (byte2 >> 4);
        if (index < sizeof(decodedData)) {
            decodedData[index++] = ((byte2 & 0xF) << 4) | (byte3 >> 2);
        }
        if (index < sizeof(decodedData)) {
            decodedData[index++] = ((byte3 & 0x3) << 6) | byte4;
        }
    }


    // Converte o conteúdo decodificado de byte para string
    decodedString = String((char*)decodedData);
    return decodedString;
}


// Função para obter o status do sensor 2 do GitHub
void obterStatusSensor2() {
    HTTPClient http;
    String url = "https://api.github.com/repos/" + repoOwner + "/" + repoName + "/contents/" + filePath;
    http.begin(url);
    http.addHeader("Authorization", "token " + token);
    http.addHeader("Accept", "application/vnd.github.v3+json");


    int httpCode = http.GET();
    if (httpCode > 0) {
        String payload = http.getString();
        Serial.println("Resposta do GitHub: ");
        Serial.println(payload);


        DynamicJsonDocument doc(1024);
        deserializeJson(doc, payload);


        // Obter o conteúdo codificado Base64 e decodificar
        String base64Content = doc["content"];
        String decodedContent = decodificarBase64(base64Content);


        // Agora, verifique o valor do status do sensor 2
        StaticJsonDocument<200> statusDoc;
        deserializeJson(statusDoc, decodedContent);
       
        // Acessando o valor de "sensor2"
        bool statusSensor2 = statusDoc["sensor2"];


        Serial.print("Status do sensor 2: ");
        Serial.println(statusSensor2 ? "true" : "false");


        // Atualizar o estado do sensor
        if (statusSensor2) {
            ledLigado = true;  // O LED só pode ser ligado se sensor2 for true
        } else {
            ledLigado = false;  // Se sensor2 for false, não liga o LED
        }
    } else {
        Serial.print("Erro ao obter o status do sensor 2. Código HTTP: ");
        Serial.println(httpCode);
    }


    http.end();
}


void enviarMensagemWhatsApp(String mensagem) {
    HTTPClient http;
   
    // Codificar a mensagem para garantir que os espaços e caracteres especiais sejam tratados
    mensagem.replace(" ", "%20");  // Substitui espaços por %20
   
    String url = "https://api.callmebot.com/whatsapp.php?phone=<INSIRA SEU NUÚMERO DE TELEFONE AQUI>&text=" + mensagem + "&apikey=6699795";
   
    http.begin(url);
    int httpCode = http.GET();


    if (httpCode == 200) {
        Serial.println("Mensagem enviada com sucesso!");
    } else {
        Serial.println("Falha ao enviar mensagem para o WhatsApp. Código HTTP: " + String(httpCode));
    }


    http.end();
}


void setup() {
    Serial.begin(115200);
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(LED_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    dht.begin();
    setup_wifi();
    client.setServer(mqttserver, mqttport);


    // Inicialização do SPIFFS
    if (!SPIFFS.begin(true)) {
        Serial.println("Erro ao montar o SPIFFS");
        return;
    }
    Serial.println("SPIFFS montado com sucesso!");


    // Lê o estado do LED armazenado no SPIFFS
    ledLigado = lerEstadoLed();
    if (ledLigado) {
        digitalWrite(LED_PIN, HIGH);
        Serial.println("LED restaurado para o estado LIGADO");
    } else {
        digitalWrite(LED_PIN, LOW);
        Serial.println("LED restaurado para o estado DESLIGADO");
    }


    // Obtém o status do sensor 2 ao iniciar
    obterStatusSensor2();
}


void loop() {
    if (!client.connected()) {
        reconnect();
    }
    client.loop();


    // Verifica o status do sensor2 no início de cada detecção
    obterStatusSensor2();  // Verifica o estado do sensor 2 a cada ciclo


    float distancia = medirDistancia();
    unsigned long tempoAtual = millis();


    // Verifica se a detecção está ocorrendo
    if (distancia > 0 && distancia < distanciaLimite) {
        if (inicioDeteccao == 0) {
            inicioDeteccao = tempoAtual;
        }


        // Verifica se a detecção continua por 6 segundos
        if (tempoAtual - inicioDeteccao >= tempoDeteccao) {
            // Verifica se o sensor 2 está "true", permitindo ligar o LED
            if (ledLigado) {
                digitalWrite(LED_PIN, HIGH);  // Acende o LED
                tempoUltimoPiscar = tempoAtual;  // Inicia o tempo de piscar
                Serial.println("Detecção de 6 segundos concluída, LED começando a piscar!");
                client.publish("Pedrin/feeds/ultrassom_status", "1");
                salvarEstadoLed(true);  // Salva o estado do LED
               
                // Enviar a mensagem para o WhatsApp
                String mensagem = "⚠️ A porta está aberta enquanto o ar-condicionado está ligado. Por favor, feche-a.";
                enviarMensagemWhatsApp(mensagem);  // Chama a função para enviar a mensagem
            } else {
                // Caso o sensor 2 esteja "false", o LED não deve ligar
                Serial.println("Sensor 2 está desligado, LED não será ligado.");
            }
        }
    } else {
        // Se a detecção terminar, reseta os tempos
        if (ledLigado) {
            ledLigado = false;
            Serial.println("Detecção terminada, LED apagado!");
            client.publish("Pedrin/feeds/ultrassom_status", "0");
            digitalWrite(LED_PIN, LOW); // Desliga o LED
            salvarEstadoLed(false);  // Salva o estado do LED
            inicioDeteccao = 0;
        }
    }


    // Se o LED está ligado, começa a piscar a cada 1 segundo
    if (ledLigado) {
        if (tempoAtual - tempoUltimoPiscar >= intervaloPiscar) {
            // Pisca o LED a cada 1 segundo
            digitalWrite(LED_PIN, !digitalRead(LED_PIN));  // Inverte o estado do LED
            tempoUltimoPiscar = tempoAtual;
        }
    }


    static unsigned long ultimoTempoDHT = 0;


    if (tempoAtual - ultimoTempoDHT >= 30000) {  // Alterado para 30 segundos
        ultimoTempoDHT = tempoAtual;
        float temperatura = dht.readTemperature();
        float umidade = dht.readHumidity();


        if (isnan(temperatura) || isnan(umidade)) {
            Serial.println("Falha na leitura do sensor DHT!");
        } else {
            char tempStr[8];
            char umidStr[8];
            dtostrf(temperatura, 1, 2, tempStr);
            dtostrf(umidade, 1, 2, umidStr);


            DynamicJsonDocument doc(1024);
           
            // Adiciona temperatura e umidade como campos separados
            doc["temperatura"] = tempStr;
            doc["umidade"] = umidStr;


            String output;
            serializeJson(doc, output);


            Serial.println(output);
            client.publish("Pedrin/feeds/ambiente", output.c_str());
        }
    }
}



