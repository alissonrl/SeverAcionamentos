
/*Autor: Alisson Rodolfo Leite
         Leandro Calixto Tenorio de Alburquerque
   
*/

//inclusão de bibliotecas
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <time.h>
#include <sys/time.h>
#include <WiFi.h>
#include <NTPClient.h> //https://github.com/arduino-libraries/NTPClient
#include <EEPROM.h>

// definição do tamanho da EEPROM a ser utilizada
#define EEPROM_SIZE 100

//-------- Configurações de Wi-fi-----------

const char* ssid     = "Rede";
const char* password = "mesmasenha";


//-----  Temperatura interna - Comandos extendidos ----
#ifdef __cplusplus
extern "C" {
#endif

uint8_t temprature_sens_read();

#ifdef __cplusplus
}
#endif

// Numero da prota do web Serever
WiFiServer server(80);

//-------- Configurações de relógio on-line-----------
WiFiUDP udp;
NTPClient ntp(udp, "a.st1.ntp.br", -3 * 3600, 60000);//Cria um objeto "NTP" com as configurações.utilizada no Brasil
String hora;            // Variável que armazena
time_t tt;              // Variavel para manipulacao auxiiar da hora
char data_formatada[64]; //auxiliar para manipulacao da hora
struct tm data;//Cria a estrutura que contem as informacoes da data.

// Variable historico das requisicoes do HTTP
String header;

// Variavel auxiliar do estado do pino de saida, ira ser utilizado para indicar na pagina web
String PinLedState = "OFF";

//Estrutura para armazenas dados do formuario
struct temporizacao {
  byte hora ;
  byte minuto;
  byte dias ;
  byte durar;
};
#define TempResfriamento 80 // temperatura o qual deve acionar ventoinha para Resfriamento 
#define PinVentoinha 11// Pino que esta conectado a ventoinha 
#define qtparam 4 // quantidade de parametros do struct, craido para facilaitar na fucao load e save
#define PinLed  2  // pino padrao que tem o led na placa 
#define maxval  5 //quantida maxia de valores de agendamento 1 - 9
#define maxpin  3 // quantida de pinos de saida que serao acionados 1 - 9 
byte saida = 1; // valor para saida default após reeboot
temporizacao tempos[maxpin][maxval];// array contendo tempos do formulario
const byte pinos_out[maxpin] = {2, 13, 14}; // pinos de saida
bool pinos_out_status[maxpin] = {false, false, false}; // variaveis de status das saidas
bool pinos_out_manual[maxpin] = {false, false, false}; // variaveis de manipulaca manual
byte tempos_acionamento[maxpin]; // array contendo a quantidade de acionamentos totais de cada pino

//Faz upload da memoria EEPROM dos dados salvos
void load() {
  int i, j;
  for (j = 0 ; j < maxpin; j++)
    for (i = 0 ; i < maxval; i++) {
      tempos[j][i].hora = EEPROM.read(i * qtparam + 0 + (j *  maxval * qtparam));
      tempos[j][i].minuto = EEPROM.read(i * qtparam + 1 + (j * maxval * qtparam));
      tempos[j][i].durar = EEPROM.read(i * qtparam + 2 + (j * maxval * qtparam));
      tempos[j][i].dias = EEPROM.read(i * qtparam + 3 + (j * maxval * qtparam));
    }

  for (i = 0 ; i < maxpin; i++) {
    tempos_acionamento[i] = EEPROM.read(maxpin * qtparam * maxval + 1 + i);
    if (tempos_acionamento[i] > maxval) tempos_acionamento[i] = 0; // travamento p primeiro start
  }
}

//salva dados de formulario na memoria EEPROM
void save() {
  int i, j;
  //primeiros espaços será para guardar os tempos
  for (j = 0 ; j < maxpin; j++)
    for (i = 0 ; i < maxval; i++) {
      EEPROM.write(i * qtparam + 0 + (j *  maxval * qtparam), tempos[j][i].hora);
      EEPROM.write(i * qtparam + 1 + (j *  maxval * qtparam), tempos[j][i].minuto);
      EEPROM.write(i * qtparam + 2 + (j *  maxval * qtparam), tempos[j][i].durar);
      EEPROM.write(i * qtparam + 3 + (j *  maxval * qtparam), tempos[j][i].dias);
    }
  // espaco para quantidade de acionamentos
  for (i = 0 ; i < maxpin; i++)
    EEPROM.write(maxpin * maxval * qtparam + 1 + i, tempos_acionamento[i]);

  EEPROM.commit(); //faz a gravacao permanente

  Serial.println("+++++++++++++++++++++++++++++++++++");
  Serial.println("carregando informações");
  for (i = 0 ; i < EEPROM_SIZE; i++) {
    Serial.print("|");
    Serial.print(byte(EEPROM.read(i)));
  }
  Serial.println("");
  Serial.println("+++++++++++++++++++++++++++++++++++");
}

void setup()
{
  int i;
  EEPROM.begin(EEPROM_SIZE); // inicia a utilizacao da EEPROM
  load(); // faz upload dos dados guardados
  Serial.begin(115200); // inicia comunicacao serial
  pinMode(PinLed, OUTPUT);      // set LED como saida
  pinMode(PinLed, OUTPUT);      // set Ventoinha como saida
  for (i = 0 ; i < maxpin; i++) {
    pinMode(pinos_out[i], OUTPUT);  // set pinos como saída
  }

  // Inicia comunicacao com Wifi
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println(WiFi.getHostname());
  Serial.println(WiFi.macAddress());

  server.begin(); // inicia serividor Web

  // cria tarefa do segundo nucleo
  xTaskCreatePinnedToCore(
    coreTaskZero,   /* função que implementa a tarefa */
    "coreTaskZero", /* nome da tarefa */
    10000,      /* número de palavras a serem alocadas para uso com a pilha da tarefa */
    NULL,       /* parâmetro de entrada para a tarefa (pode ser NULL) */
    1,          /* prioridade da tarefa (0 a N) */
    NULL,       /* referência para a tarefa (pode ser NULL) */
    0);         /* Núcleo que executará a tarefa */
}

void coreTaskZero( void * pvParameters ) {
  timeval tv;//Cria a estrutura temporaria para funcao abaixo.
  ntp.begin();               // Inicia o protocolo NTP
  ntp.forceUpdate();    // Atualização .
  hora = ntp.getFormattedTime(); // Pega hora formatada como string
  Serial.println(hora);     // Escreve a hora no monitor serial.
  tv.tv_sec = ntp.getEpochTime(); // pega a quantidade de segundos
  settimeofday(&tv, NULL);//Configura o RTC para manter a data atribuida atualizada.
  Serial.println(ntp.getEpochTime()); // imprime a quantidade de segundos
  int j, i;
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));//Espera 1 seg
    tt = time(NULL);//Obtem o tempo atual em segundos. Utilize isso sempre que precisar obter o tempo atual
    data = *gmtime(&tt);//Converte o tempo atual e atribui na estrutura
    strftime(data_formatada, 64, "%d/%m/%Y %H:%M:%S", &data);//Cria uma String formatada da estrutura "data"
    //printf("\nUnix Time: %d\n", int32_t(tt));//Mostra na Serial o Unix time
    //printf("Data formatada: %s\n", data_formatada);//Mostra na Serial a data formatada
    int hora = data.tm_hour, minutos = data.tm_min ; // auxiliar da hora atual
    int agora = hora * 60  + minutos; // axiliar dos minutos atuais
    //Serial.print("Valor de agora : ");
    //Serial.println(agora);
    bool ligauto = false; //axiliar de controle
    for (j = 0 ; j < maxpin; j++) { // ficara o condional para criação de mais bicos
      for (i = 0; i < tempos_acionamento[j] ; i++) { // FAZ SCANNER EM TODOS OS TEMPOS DE ACIONAMENTO HABILITADOS
        int acionamento_inicial = ((tempos[j][i].hora * 60) + tempos[saida][i].minuto); // auxiliar de acionamento inicial
        int acionamento_final = ((tempos[j][i].hora * 60) + tempos[saida][i].minuto + tempos[saida][i].durar); // auxiliar de acionamento final
        if ((acionamento_inicial <= agora) && (agora < acionamento_final)) {
          ligauto = true; // se em alguma situacao precisar ligar seta para true
        }
        //Serial.print("Valor Inicial: ");
        //Serial.println(acionamento_inicial);
        //Serial.print("Valor Final: ");
        //Serial.println(acionamento_final);
      }
      if ((ligauto) || (pinos_out_manual[j])) { // se caso tiver dentro do intervalo ou se for requisitado acionamento manual liga a saida
        digitalWrite (pinos_out[j], HIGH); // liga saida
        pinos_out_status[j] = true; // atualiza status no array
      } else {
        digitalWrite (pinos_out[j], LOW); // liga saida
        pinos_out_status[j] = false; // atualiza status no array
      }
      // Dsiplay da saida atual
      if (pinos_out_status[saida]) {
        PinLedState = "ON";
      } else {
        PinLedState = "OFF";
      }
    }
  }
  //acionamneto de ventoinha para resifriamento
  if (((temprature_sens_read() - 32) / 1.8) > TempResfriamento ) {
    digitalWrite (PinVentoinha, HIGH); // liga resfriamnto
  } else if (((temprature_sens_read() - 32) / 1.8) < (TempResfriamento - 5)) {
    digitalWrite (PinVentoinha, LOW); //desliga resfriamento
  }
}
// tratamento de informacao
String tratamento(int indice) {
  //Tratamento
  int hora_t, minutos_t;
  char char_aux[6];
  hora_t = tempos[saida][indice].hora;
  minutos_t = tempos[saida][indice].minuto;
  sprintf(char_aux, "%02d:%02d", hora_t, minutos_t);
  return char_aux;
}

void loop() {
  String str_aux, str_aux1;
  char char_aux[6];
  int i, pos;
  WiFiClient client = server.available();   // verifica se tem clientes conectados
  if (client) {                             // Se existir cliente conectado ,
    Serial.println("Novo Cliente Conectado");           // Imprime novo cliente na serial
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,

        char c = client.read();             // read a byte, then
        Serial.write(c);                    // print it out the serial monitor
        header += c;
        if (c == '\n') {                    // if the byte is a newline character

          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();

            // turns the GPIOs on and off
            if (header.indexOf("GET /2/on") >= 0) {
              Serial.println("GPIO 2 on");
              pinos_out_manual[saida] = true;
              PinLedState = "ON";
            } else if (header.indexOf("GET /2/off") >= 0) {
              Serial.println("GPIO 2 off");
              pinos_out_manual[saida] = false;
              PinLedState = "OFF";
            }
            if (header.indexOf("AGENDA") > 0) {
              for (i = 0; i < maxval ; i++) {
                //Pega a hora e minuto que deve acontecer o acionamento
                pos = header.indexOf("tempo" + String(i));
                if (pos > 0) {
                  tempos[saida][i].hora = header.substring(pos + 7, pos + 9).toInt();
                  tempos[saida][i].minuto = header.substring(pos + 12, pos + 15).toInt();
                }//Pega o empo de duração de acionamento
                pos = header.indexOf("durar" + String(i));
                if (pos > 0) {
                  tempos[saida][i].durar = header.substring(pos + 7, pos + header.indexOf("&", pos)).toInt();
                }
              }
              //salva informações na eeprom
              save();
            }

            if (header.indexOf("REFRESH") > 0) {
              for (i = 0; i < maxpin ; i++) {
                pos = header.indexOf("saida" + String(i));
                if (pos > 0) {
                  Serial.print("Tempo de Agendamento = ");
                  Serial.println(header.substring(pos + 7, pos + 8)); //alterar aqui se caso quiser mais que 9 tempos
                  tempos_acionamento[saida] = header.substring(pos + 7, pos + 8).toInt();
                }
              }
              //salva informações na eeprom
              save();
            }
            pos = header.indexOf("OUTPUT");
            if (pos > 0) {
              saida = header.substring(pos + 6, pos + 7).toInt();
            }

            // Display the HTML web page
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta charset='utf-8' name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            // CSS to style the on/off buttons
            // Feel free to change the background-color and font-size attributes to fit your preferences
            client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
            client.println(".button { background-color: #4CAF50; border: none; color: white; padding: 16px 40px;");
            client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
            client.println(".button2 {background-color: #555555;}</style></head>");

            // Web Page Heading
            str_aux = data_formatada; 
            str_aux1 = String(((temprature_sens_read() - 32) / 1.8)) + "C";//leitura do sensor de temperatura interno
            str_aux = str_aux + " - " + str_aux1;
            client.println("<body><h1>ESP32 Web Server</h1>");
            // por selecao de saida checked='checked'
            client.println("<p>");
            for (i = 0 ; i < maxpin ; i++) {
              if (saida == i) {//Criacao do raiobox com check na saida que estiver ativada
                client.println("<a href=\"/OUTPUT" + String(i) + "\"><button class=\"button\">S" + String(i + 1) + "</button></a>");
              } else {
                client.println("<a href=\"/OUTPUT" + String(i) + "\"><button class=\"button button2\">S" + String(i + 1) + "</button></a>");
              }
            }
            client.println("</p>");

            client.println("<body><h2>" + str_aux + "</h2>");
            client.println("<p>GPIO " + String(pinos_out[saida]) + " - State " + PinLedState + "</p>");

            // If the PinLedState is off, it displays the ON button
            if (PinLedState == "OFF") {
              client.println("<p><a href=\"/2/on\"><button class=\"button\">ON</button></a></p>");
            } else {
              client.println("<p><a href=\"/2/off\"><button class=\"button button2\">OFF</button></a></p>");
            }

            str_aux = tempos_acionamento[saida] ;
            client.println("<form method='get'>");
            client.println("  <b>Periodos de Acionamento:</b><input type='number' step ='1' min='1' max='" + String(maxval) + "' name='saida" + String(saida) + "' maxlength='3' value='" + str_aux + "'size='5'/>");
            client.println("  <input type='submit' name='bt' value='REFRESH'/>");
            client.println("</form>");

            client.println("      <form method='get'>");
            client.println("         <table align='center'>");
            client.println("            <tr>");
            client.println("               <td>");
            client.println("                  <font size='4'><b><u>Agendamento de Acionamento Diário</u></b></font>");
            client.println("               </td>");
            for (i = 0 ; i < tempos_acionamento[saida] ; i++) {
              client.println("            <tr>");
              client.println("               <td>");
              client.println("                  <b>Início:</b> <input type='time' name='tempo" + String(i) + "' maxlength='2' size='5' value = '" + tratamento(i) + "'/> hora(s)");
              client.println("               </td>");
              client.println("            </tr>");
              client.println("            <tr>");
              client.println("               <td>");
              str_aux = tempos[saida][i].durar;
              client.println("                  <b>Duração:</b><input type='number' step ='1' min='0' max='255' name='durar" + String(i) + "' maxlength='3' value='" + str_aux + "'size='5'/> minuto(s)");
              client.println("               </td>");
              client.println("            </tr>");
              client.println("            <tr>");
              client.println("               <td>");
              client.println("               </td>");
              client.println("            </tr>");
            }
            client.println("            <tr>");
            client.println("               <td align='center'>");
            client.println("                  <input type='submit' name='btag' value='AGENDAR'/>");
            client.println("               </td>");
            client.println("            </tr>");
            client.println("         </table>");
            client.println("      </form>");
            client.println("</body></html>");

            // The HTTP response ends with another blank line

            client.println();
            // break out of the while loop:
            break;
          } else {    // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
    // Clear the header variable
    header = "";
    // close the connection:
    client.stop();
    Serial.println("Client Disconnected.");
  }
}
