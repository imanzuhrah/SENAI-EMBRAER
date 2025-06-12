
/*
Projeto: Monitoramento de Enchente - SENAI Instituto Embraer
Local: SENAI Botucatu
Curso: CT Eletroeletrônica
Autor: Prof. Henrique / Helder; Alunas: Camilli e Iman
Descrição: Sistema de alerta de enchente usando sensor ultrassônico, WiFi e previsão de clima da Open-Meteo, com melhorias.
*/

// --- INCLUSÃO DE BIBLIOTECAS ---
#include <WiFiS3.h>       // Biblioteca WiFi específica do Arduino R4 WiFi
#include <ArduinoJson.h>  // Biblioteca para processamento seguro de JSON

// --- CONFIGURAÇÕES DE HARDWARE ---
#define TRIG_PIN 2   // Pino de Trigger do sensor ultrassônico
#define ECHO_PIN 3   // Pino de Echo do sensor ultrassônico

// --- CONFIGURAÇÕES DE WIFI ---
const char* ssid = "Henrique";      // Nome da rede WiFi
const char* password = "rique123";         // Senha da rede WiFi

// Configuração de IP fixo
IPAddress local_IP(10, 90, 108, 184);
IPAddress gateway(10, 90, 108, 1);
IPAddress subnet(255, 255, 255, 0);

WiFiClient client;        // Objeto cliente para conexões HTTP
WiFiServer server(80);    // Servidor Web na porta 80

// --- VARIÁVEIS DE SENSOR ---
long duration;
float distance;
float previousDistance;
unsigned long lastMeasureTime;

// --- VARIÁVEIS DE CLIMA ---
float temperaturaAtual = 0;
float velocidadeVento = 0;
float umidadeRelativa = 0;

// --- PARÂMETROS DE ALERTA ---
const float ALERT_THRESHOLD = 5.0;             // Variação mínima em cm/minuto para indicar enchente
const unsigned long INTERVAL = 60000;          // Intervalo de medição (1 minuto)

bool alertaEnchente = false;
bool alertaMeteorologia = false;

// --- SETUP ---
void setup() {
  Serial.begin(115200); // Inicializa comunicação serial para debug

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  connectWiFi();         // Tenta conectar à rede WiFi
  server.begin();        // Inicia o servidor Web

  previousDistance = measureDistance(); // Primeira medição do nível do rio
  lastMeasureTime = millis();            // Armazena tempo da primeira medição
}

// --- LOOP PRINCIPAL ---
void loop() {
  // Executa uma nova medição a cada INTERVALO
  if (millis() - lastMeasureTime >= INTERVAL) {
    float currentDistance = measureDistance();
    float delta = previousDistance - currentDistance;
    previousDistance = currentDistance;

    alertaEnchente = (delta >= ALERT_THRESHOLD); // Verifica se há variação significativa no nível

    alertaMeteorologia = verificaClima(); // Atualiza informações meteorológicas

    lastMeasureTime = millis(); // Atualiza tempo da última medição
  }

  serveWebClient(); // Atende eventuais requisições web
}

// --- FUNÇÃO PARA CONECTAR AO WIFI ---
void connectWiFi() {
  Serial.print("Conectando a ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi conectado!");
  Serial.print("IP Local: ");
  Serial.println(WiFi.localIP());
}

// --- FUNÇÃO PARA MEDIR A DISTÂNCIA ULTRASSÔNICA ---
float measureDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(5);
  digitalWrite(TRIG_PIN, LOW);

  duration = pulseIn(ECHO_PIN, HIGH, 30000); // Tempo máximo de 30ms (para evitar travamentos)

  distance = (duration * 0.034) / 2; // Converte tempo em distância (cm)

  if (distance == 0 || distance > 400) {
    Serial.println("Leitura inválida do sensor!");
    return previousDistance; // Retorna a última medida válida
  }

  Serial.print("Distância medida: ");
  Serial.print(distance);
  Serial.println(" cm");
  
  return distance;
}

// --- FUNÇÃO PARA VERIFICAR O CLIMA USANDO API OPEN-METEO ---
bool verificaClima() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi(); // Reconecta se WiFi estiver desconectado
  }

  const char* host = "api.open-meteo.com";
  const int httpPort = 80;

  if (client.connect(host, httpPort)) {
    String url = "/v1/forecast?latitude=-8.0407&longitude=-34.9601&current=temperature_2m,wind_speed_10m,relative_humidity_2m";

    client.println("GET " + url + " HTTP/1.1");
    client.println("Host: api.open-meteo.com");
    client.println("Connection: close");
    client.println();

    String payload = "";
    bool jsonStarted = false;
    unsigned long timeout = millis();

    // Lê a resposta HTTP
    while (client.connected() && (millis() - timeout < 5000)) { // Timeout de 5 segundos
      while (client.available()) {
        char c = client.read();
        if (c == '{') jsonStarted = true;
        if (jsonStarted) payload += c;
      }
    }
    client.stop(); // Encerra conexão com o servidor

    if (payload.length() > 0) {
      StaticJsonDocument<1024> doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (!error) {
        temperaturaAtual = doc["current"]["temperature_2m"] | 0.0;
        velocidadeVento = doc["current"]["wind_speed_10m"] | 0.0;
        umidadeRelativa = doc["current"]["relative_humidity_2m"] | 0.0;

        Serial.println("--- Dados meteorológicos atualizados ---");
        Serial.print("Temperatura: ");
        Serial.println(temperaturaAtual);
        Serial.print("Vento: ");
        Serial.println(velocidadeVento);
        Serial.print("Umidade: ");
        Serial.println(umidadeRelativa);

        // Critério de alerta de enchente meteorológica
        if (umidadeRelativa >= 85.0 && velocidadeVento >= 8.0) {
          return true;
        }
      } else {
        Serial.print("Erro no parse do JSON: ");
        Serial.println(error.c_str());
      }
    } else {
      Serial.println("Resposta vazia da API Open-Meteo.");
    }
  } else {
    Serial.println("Falha na conexão com Open-Meteo");
  }
  return false;
}
void serveApiClient(WiFiClient client) {
  StaticJsonDocument<512> doc;

  doc["temperatura"] = temperaturaAtual;
  doc["umidade"] = umidadeRelativa;
  doc["vento"] = velocidadeVento;
  doc["nivel_rio"] = previousDistance;
  doc["alerta_enchente"] = alertaEnchente;
  doc["alerta_meteorologico"] = alertaMeteorologia;

  String jsonString;
  serializeJson(doc, jsonString);

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println(jsonString);

  client.stop();
}


// --- FUNÇÃO PARA ATENDER CLIENTES WEB E GERAR A PÁGINA HTML ---
void serveWebClient()
{
  WiFiClient client = server.available();
  if (client)
  {
    while (client.connected())
    {
      if (client.available())
      {
        String request = client.readStringUntil('\r');
        Serial.println(request);
        client.flush();
        if (request.indexOf("GET /data") >= 0)
        {
          serveApiClient(client);
          return;
        }

        // Enviar cabeçalho HTTP
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html");
        client.println("Connection: close");
        client.println();

        // Aqui enviamos TODO o HTML direto.
        client.println(F(R"rawliteral(




<!DOCTYPE html> 
<html lang="pt-BR">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Monitoramento de Enchente</title>
  <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css" />
  <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/bootstrap-icons@1.10.5/font/bootstrap-icons.css" />
<style>
  body {
    font-family: Trebuchet MS;
    background-color: navy;
    color: #F5F5F5;
    margin: 0;
    padding: 0;
  }

  header {
    background-color: #104E8B;
    padding: 10px 20px;
  }

  .header-top {
    display: flex;
    align-items: center;
    justify-content: space-between;
    flex-wrap: wrap;
  }

  .logo-container {
    display: flex;
    align-items: center;
    gap: 15px;
  }

  .logo-pe {
    width: 90px;
    height: auto;
    border: 2px solid #fff;
    border-radius: 5px;
  }

  .logo-senai {
    width: 100px;
    height: auto;
    border: 2px solid #fff;
    border-radius: 5px;
  }

  .titulo {
    font-size: 1.8em;
    font-weight: bold;
  }

  .top-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 20px;
    flex-wrap: wrap;
  }

  .update-card-header {
    background: #17a2b8;
    color: white;
    padding: 8px 12px;
    border-radius: 6px;
    font-weight: 600;
    font-size: 1.1em;
    user-select: none;
    box-shadow: 0 0 5px rgba(0, 0, 0, 0.3);
    max-width: 160px;
    text-align: center;
    margin-bottom: 10px;
  }

  .status-circle {
    display: flex;
    align-items: center;
    justify-content: center;
    text-align: center;
    border-radius: 50%;
    width: 150px;
    height: 150px;
    padding: 10px;
  }

  .circle-text {
    white-space: pre-line;
    font-size: calc(10px + 0.5vw);
    line-height: 1.2;
    font-weight: bold;
  }

  .thermometer {
    width: 40px;
    height: 150px;
    background: #ccc;
    border-radius: 15px;
    position: relative;
    overflow: hidden;
    border: 2px solid #555;
  }

  .thermometer-fill {
    position: absolute;
    bottom: 0;
    width: 100%;
    transition: height 0.5s ease-in-out, background 0.5s;
    height: 0%;
  }

  .temp-frio {
    background: #00f;
  }

  .temp-ameno {
    background: #0cf;
  }

  .temp-quente {
    background: #f90;
  }

  .temp-muito-quente {
    background: #f00;
  }

  .legend-box {
    display: inline-block;
    width: 14px;
    height: 14px;
    margin-right: 6px;
    vertical-align: middle;
    border: 1px solid #fff;
    border-radius: 2px;
  }

  .card-vertical {
    height: 300px;
    display: flex;
    flex-direction: column;
    justify-content: center;
  }

  .icon-custom {
    color: #0056b3;
    text-shadow: 2px 2px 2px #000;
    font-size: 4em;
    animation: swing 2s infinite ease-in-out;
    display: inline-block;
  }

  .value-large {
    font-size: 3em;
    font-weight: bold;
    margin-top: 10px;
  }

  .card h2 {
    min-height: 2.2em;
    display: flex;
    align-items: center;
    justify-content: center;
    margin-bottom: 1rem;
  }

  .progress-bar {
    transition: width 0.5s ease-in-out;
  }

  @keyframes swing {
    0% { transform: rotate(0deg); }
    25% { transform: rotate(5deg); }
    50% { transform: rotate(-5deg); }
    75% { transform: rotate(3deg); }
    100% { transform: rotate(0deg); }
  }

  @media (max-width: 768px) {
    body {
      font-family: Trebuchet MS;
      background-color: navy;
      color: #F5F5F5;
      margin: 0;
      padding: 0;
    }

    .card {
      background-color: #1E90FF;
      font-family: Cambodian;
      color: #F5F5F5;
    }

    .top-row {
      flex-direction: column;
      align-items: center;
      gap: 15px;
    }

    .card-vertical {
      height: auto;
    }

    .header-top {
      justify-content: center;
      gap: 15px;
      text-align: center;
      flex-direction: column;
    }

    .logo-container {
      justify-content: center;
    }
  }
</style>

</head>
<body>
  <header>
    <div class="header-top">
      <div class="left-logos" style="display: flex; align-items: center;">
        <img src="https://4.bp.blogspot.com/-jX9ma4eoYK4/WP9T8GnBZMI/AAAAAAAAUAM/MLC4ALSxbz0VaWMDIIxUAbYIak67Yxm8gCLcB/w1200-h630-p-k-no-nu/1200px-Bandeira_de_Pernambuco.png" alt="Mapa de Pernambuco" class="logo-pe" />
      </div>
      <div class="titulo">Monitoramento de Enchente - Rio Capibaribe</div>
      <div class="logo-container">
        <img src="https://acontecebotucatu.com.br/wp-content/uploads/2019/01/sendNoticia-330x159.jpg" alt="Logo Colégio Embraer Botucatu" class="logo-pe" />
        <img src="https://www.sp.senai.br/images/senai.svg" alt="Logo SENAI" class="logo-senai" />
      </div>
    </div>
  </header>
  <main>
    <div class="top-row">
      <div class="update-card-header ultima-atualizacao">Última Atualização<br /><strong>--:--</strong></div>
      <div class="status-circle"><span class="circle-text">--</span></div>
      <div style="width: 160px"></div>
    </div>
    <div class="container my-4">
      <div class="row g-4 align-items-stretch">
        <div class="col-md-4 d-flex align-items-center">
          <div class="card text-center text-dark p-3 card-vertical flex-grow-1 w-100" style="background-color: #1E90FF; font-size: bold;">
            <h2>Temperatura</h2>
            <div class="value-temp" style="font-size: 1.6em; font-weight: bold;">--°C</div>
            <div class="d-flex justify-content-center align-items-center mt-3 gap-3">
              <div class="thermometer">
                <div class="thermometer-fill" style="height: 0%"></div>
              </div>
              <div class="text-start small text-white" style="line-height: 1.4">
                <div><span class="legend-box bg-primary"></span> Frio (&lt;15°C)</div>
                <div><span class="legend-box" style="background: #0cf"></span> Ameno (15–24°C)</div>
                <div><span class="legend-box" style="background: #f90"></span> Quente (25–34°C)</div>
                <div><span class="legend-box bg-danger"></span> Muito Quente (≥35°C)</div>
              </div>
            </div>
          </div>
        </div>
        <div class="col-md-8">
          <div class="row g-4">
            <div class="col-md-6 d-flex">
              <div class="card text-center text-dark p-3 w-100 card-vertical" style="background-color: #1E90FF; font-size: bold;">
                <h2>Umidade Relativa</h2>
                <div><i class="bi bi-droplet icon-custom"></i><br /><span class="value-large value-umidade">--%</span></div>
              </div>
            </div>
            <div class="col-md-6 d-flex">
              <div class="card text-center text-dark p-3 w-100 card-vertical" style="background-color: #1E90FF; font-size: bold;">
                <h2>Velocidade do Vento</h2>
                <div><i class="bi bi-wind icon-custom"></i><br /><span class="value-large value-vento">--m/s</span></div>
              </div>
            </div>
          </div>
        </div>
        <div class="col-12 d-flex mt-4">
          <div class="card text-center text-dark p-4 w-100" style="background-color: #1E90FF; font-size: bold;">
            <h2>Nível do Rio</h2>
            <div class="value-rio" style="font-size: 1.6em; font-weight: bold;">-- cm</div>
            <div class="progress mt-3" role="progressbar" aria-valuenow="0" aria-valuemin="0" aria-valuemax="200" style="height: 25px">
              <div class="progress-bar bg-primary" style="width: 0%"></div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </main>
  <footer style="background-color: #104E8B; color: #F5F5F5; text-align: center; padding: 12px 20px; font-size: 0.9em;">
    Desenvolvido por SENAI Botucatu e Instituto Embraer Botucatu - Open Source.
  </footer>

<script>
function atualizarDados() {
  fetch('/data')
    .then(response => response.json())
    .then(data => {
      document.querySelector('.value-temp').textContent = data.temperatura + '°C';
      document.querySelector('.value-umidade').textContent = data.umidade + '%';
      document.querySelector('.value-vento').textContent = data.vento + 'm/s';
      document.querySelector('.value-rio').textContent = data.nivel_rio + ' cm';

      // Barra de progresso - cresce da esquerda para a direita
      const progressBar = document.querySelector('.progress-bar');
      const nivelMax = 200; 
      const nivelAtual = data.nivel_rio;

      // Quanto mais próximo do sensor, maior a barra (inverso)
      let progress = ((nivelMax - nivelAtual) / nivelMax) * 100;
      progress = Math.max(0, Math.min(progress, 100));

      progressBar.style.width = progress + '%';
      progressBar.setAttribute('aria-valuenow', progress);

      // Cores de alerta conforme a proximidade
      const statusCircle = document.querySelector('.status-circle');
      const circleText = document.querySelector('.circle-text');

      if (progress >= 90) {
  // Muito próximo: alerta vermelho
  statusCircle.style.backgroundColor = '#dc3545';
  statusCircle.style.boxShadow = '0 0 15px #b02a37';
  circleText.textContent = 'ALERTA!\nTRANSBORDANDO';
} else if (progress >= 70) {
  // Atenção: laranja
  statusCircle.style.backgroundColor = '#fd7e14';
  statusCircle.style.boxShadow = '0 0 15px #e8590c';
  circleText.textContent = 'ATENÇÃO:\nNÍVEL ALTO';
} else if (progress >= 40) {
  // Atenção: amarelo
  statusCircle.style.backgroundColor = '#FFD700';
  statusCircle.style.boxShadow = '0 0 15px #e8590c';
  circleText.textContent = 'ATENÇÃO:\nNÍVEL MODERADO';
} else {
  // Seguro: verde
  statusCircle.style.backgroundColor = '#28a745';
  statusCircle.style.boxShadow = '0 0 15px #1d7a34';
  circleText.textContent = 'SEGURO';
}


      // Termômetro
      const termometro = document.querySelector('.thermometer-fill');
      let height = 0;
      if (data.temperatura < 15) {
        termometro.className = 'thermometer-fill temp-frio';
        height = 25;
      } else if (data.temperatura < 25) {
        termometro.className = 'thermometer-fill temp-ameno';
        height = 50;
      } else if (data.temperatura < 35) {
        termometro.className = 'thermometer-fill temp-quente';
        height = 75;
      } else {
        termometro.className = 'thermometer-fill temp-muito-quente';
        height = 100;
      }
      termometro.style.height = height + '%';

      // Horário da última atualização
      const agora = new Date();
      const horas = agora.getHours().toString().padStart(2, '0');
      const minutos = agora.getMinutes().toString().padStart(2, '0');
      const segundos = agora.getSeconds().toString().padStart(2, '0');
      const horario = `${horas}:${minutos}:${segundos}`;

      document.querySelector('.ultima-atualizacao').textContent = horario;
    })
    .catch(err => console.error('Erro ao atualizar dados:', err));
}

setInterval(atualizarDados, 5000);
atualizarDados();
</script>

</body>
</html>
)rawliteral"));

        break;
      }
    }
    delay(1);
    client.stop();
    Serial.println("Cliente desconectado");
  }
}
