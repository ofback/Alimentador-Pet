#include <WiFi.h>
#include <ESP32Servo.h>
#include <Firebase_ESP_Client.h>

// Funções auxiliares da biblioteca Firebase (para o status do token)
#include <addons/TokenHelper.h>

// --- Configurações do Wi-Fi ---
#define WIFI_SSID "iPhone de Matheus"
#define WIFI_PASSWORD "mana3074"

// --- Configurações do Firebase (Baseadas nas suas informações) ---
#define API_KEY "AIzaSyBvQp-7ofiLzJESGtFVao5yQQhSM3eNhEE"
#define DATABASE_URL "https://projetofinal-db140-default-rtdb.firebaseio.com"
// #define FIREBASE_PROJECT_ID "projetofinal-db140" // Para referência, não usado diretamente na config básica

// --- Configurações dos Servos ---
const int SERVO_PIN_1 = 13; // Pino para o servo do compartimento 1
const int SERVO_PIN_2 = 14; // Pino para o servo do compartimento 2
const int SERVO_ABERTO_POS = 90; // Posição do servo quando aberto
const int SERVO_FECHADO_POS = 0;  // Posição do servo quando fechado
const int TEMPO_ABERTO_MS = 5000; // Tempo que o compartimento fica aberto (5 segundos)

// Objetos Servo
Servo servo1;
Servo servo2;

// Objetos Firebase
FirebaseData fbdo_comp1; // Objeto FirebaseData para o compartimento 1
FirebaseData fbdo_comp2; // Objeto FirebaseData para o compartimento 2
FirebaseData fbdo_reset; // Objeto FirebaseData para resetar os flags
FirebaseAuth auth;
FirebaseConfig config;

// Variáveis de controle de tempo
unsigned long previousMillisFirebase = 0;
const long firebaseInterval = 2000; // Intervalo para checar o Firebase (2 segundos)

bool firebaseReady = false;

void setup() {
    Serial.begin(115200);
    while (!Serial) { delay(100); } // Espera o Serial estar pronto
    Serial.println();
    Serial.println("Iniciando Alimentador Pet IoT...");

    // Configura os servos
    servo1.attach(SERVO_PIN_1);
    servo2.attach(SERVO_PIN_2);
    servo1.write(SERVO_FECHADO_POS);
    servo2.write(SERVO_FECHADO_POS);
    Serial.println("Servos configurados e na posição inicial.");

    // Conecta ao Wi-Fi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Conectando ao Wi-Fi ");
    unsigned long wifiConnectStart = millis();
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(500);
        if (millis() - wifiConnectStart > 20000) { // Timeout de 20 segundos
            Serial.println("\nFalha ao conectar ao Wi-Fi. Verifique as credenciais e o sinal.");
            Serial.println("Reiniciando em 10 segundos...");
            delay(10000);
            ESP.restart();
        }
    }
    Serial.println("\nWi-Fi conectado!");
    Serial.print("Endereço IP: ");
    Serial.println(WiFi.localIP());

    // Configura Firebase
    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;
    config.token_status_callback = tokenStatusCallback; // Necessário para `addons/TokenHelper.h`

    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    // Definindo timeouts (opcional, mas bom para robustez)
    // Firebase.RTDB.setReadTimeout(&fbdo_comp1, 2000); // Timeout de 2s para leitura
    // Firebase.RTDB.setReadTimeout(&fbdo_comp2, 2000);
    // Firebase.RTDB.setWriteTimeout(&fbdo_reset, 2000); // Timeout de 2s para escrita

    Serial.println("Aguardando autenticação do Firebase...");
    unsigned long firebaseAuthStart = millis();
    while (auth.token_status != token_status_ready) {
        Serial.print("*"); // Indica que está aguardando o token
        delay(1000);
        if (millis() - firebaseAuthStart > 30000) { // Timeout de 30 segundos
            Serial.println("\nFalha na autenticação do Firebase (Timeout).");
            Serial.printf("Status do Token: %s\n", Firebase.getTokenInfo(auth.token_info.status).c_str());
            Serial.printf("Erro do Token: %s\n", Firebase.getTokenInfo(auth.token_info.error.message).c_str());
            Serial.println("Verifique API Key, Database URL e Regras de Segurança.");
            Serial.println("Reiniciando em 10 segundos...");
            delay(10000);
            ESP.restart();
        }
    }
    Serial.println("\nFirebase autenticado e pronto.");
    firebaseReady = true;
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi desconectado. Tentando reconectar...");
        // A biblioteca Firebase já tenta reconectar o WiFi se Firebase.reconnectWiFi(true) foi chamado.
        delay(5000); // Espera um pouco antes de checar de novo
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("Reconexão falhou. Reiniciando...");
            delay(1000);
            ESP.restart();
        }
        return; // Sai do loop atual para tentar reconectar na próxima iteração
    }

    if (!firebaseReady) {
        Serial.println("Firebase não está pronto. Verifique a configuração.");
        delay(5000);
        // Tentar re-autenticar ou reiniciar pode ser uma opção aqui
        // Por ora, vamos apenas aguardar e na próxima falha de WiFi ele reinicia.
        return;
    }

    unsigned long currentMillis = millis();
    if (currentMillis - previousMillisFirebase >= firebaseInterval) {
        previousMillisFirebase = currentMillis;

        // --- Processa Compartimento 1 ---
        // Serial.println("Verificando /compartimento1"); // Para depuração
        if (Firebase.RTDB.getBool(&fbdo_comp1, "/compartimento1")) {
            if (fbdo_comp1.dataTypeEnum() == fb_esp_rtdb_data_type_boolean && fbdo_comp1.boolData()) {
                Serial.println("Comando para abrir Compartimento 1 recebido!");
                servo1.write(SERVO_ABERTO_POS);
                Serial.printf("Compartimento 1: Servo movido para %d graus.\n", SERVO_ABERTO_POS);
                delay(TEMPO_ABERTO_MS); // Mantém aberto pelo tempo definido

                servo1.write(SERVO_FECHADO_POS);
                Serial.printf("Compartimento 1: Servo retornado para %d graus.\n", SERVO_FECHADO_POS);

                // Reseta o flag no Firebase
                if (Firebase.RTDB.setBool(&fbdo_reset, "/compartimento1", false)) {
                    Serial.println("Compartimento 1: Flag resetado no Firebase.");
                } else {
                    Serial.println("Compartimento 1: ERRO ao resetar flag - " + fbdo_reset.errorReason());
                }
            }
        } else {
             // Não imprima erros de leitura toda vez, apenas se for um erro persistente.
             // Serial.println("Falha ao ler /compartimento1: " + fbdo_comp1.errorReason());
        }

        // --- Processa Compartimento 2 ---
        // Serial.println("Verificando /compartimento2"); // Para depuração
        if (Firebase.RTDB.getBool(&fbdo_comp2, "/compartimento2")) {
            if (fbdo_comp2.dataTypeEnum() == fb_esp_rtdb_data_type_boolean && fbdo_comp2.boolData()) {
                Serial.println("Comando para abrir Compartimento 2 recebido!");
                servo2.write(SERVO_ABERTO_POS);
                Serial.printf("Compartimento 2: Servo movido para %d graus.\n", SERVO_ABERTO_POS);
                delay(TEMPO_ABERTO_MS);

                servo2.write(SERVO_FECHADO_POS);
                Serial.printf("Compartimento 2: Servo retornado para %d graus.\n", SERVO_FECHADO_POS);

                if (Firebase.RTDB.setBool(&fbdo_reset, "/compartimento2", false)) {
                    Serial.println("Compartimento 2: Flag resetado no Firebase.");
                } else {
                    Serial.println("Compartimento 2: ERRO ao resetar flag - " + fbdo_reset.errorReason());
                }
            }
        } else {
            // Serial.println("Falha ao ler /compartimento2: " + fbdo_comp2.errorReason());
        }
    }
    // Pequeno delay para não sobrecarregar o loop principal se nada mais for feito
    // No entanto, o "firebaseInterval" já controla a frequência das chamadas ao Firebase.
    // delay(100);
}