
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "freertos/queue.h"

#define DEFAULT_VREF    1100        //Use adc2_vref_to_gpio() to obtain a better estimate
#define NO_OF_SAMPLES   20          //Multisampling

static esp_adc_cal_characteristics_t *adc_chars;
//#if CONFIG_IDF_TARGET_ESP32
//Configuracion de los canales de adc
static const adc_channel_t channel = ADC_CHANNEL_6;     //GPIO34 if ADC1, GPIO14 if ADC2
static const adc_bits_width_t width = ADC_WIDTH_BIT_12;

static const adc_atten_t atten = ADC_ATTEN_11db;    //Atenuacion de 11db para recibir en la entrada entre 0-3.3V
static const adc_unit_t unit = ADC_UNIT_1;



//Manejador de colas
xQueueHandle cola_LED_recibido;
xQueueHandle cola_frecuencia;
xQueueHandle cola_bien_mal;




//-------------------Configuraciones del ADC del ejemplo ADC1-----------------------------------------
static void check_efuse(void)
{
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK) {
        printf("eFuse Two Point: Supported\n");
    } else {
        printf("eFuse Two Point: NOT supported\n");
    }
    //Check Vref is burned into eFuse
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF) == ESP_OK) {
        printf("eFuse Vref: Supported\n");
    } else {
        printf("eFuse Vref: NOT supported\n");
    }

}


static void print_char_val_type(esp_adc_cal_value_t val_type)
{
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        printf("Characterized using Two Point Value\n");
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        printf("Characterized using eFuse Vref\n");
    } else {
        printf("Characterized using Default Vref\n");
    }
}




//-------------------TAREAS-----------------------------------------



void receptorADC(void* arg)     //Tarea 1
{

    //Parte ADC example
    //Check if Two Point or Vref are burned into eFuse
    check_efuse();

    //Configure ADC
    if (unit == ADC_UNIT_1) {
        adc1_config_width(width);
        adc1_config_channel_atten(channel, atten);
    } else {
        adc2_config_channel_atten((adc2_channel_t)channel, atten);
    }

    //Characterize ADC
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, atten, width, DEFAULT_VREF, adc_chars);
    print_char_val_type(val_type);

    char cadena[3];
    int Contador;
    
    Contador=0;
    //Continuously sample ADC1
    while (1) {
        uint32_t adc_reading = 0;
        //Multisampling
        for (int i = 0; i < NO_OF_SAMPLES; i++) {       // Solo hace varias muestras de la entrada de adc del pin 34
            if (unit == ADC_UNIT_1) {
                adc_reading += adc1_get_raw((adc1_channel_t)channel);
            } else {
                int raw;
                adc2_get_raw((adc2_channel_t)channel, width, &raw);
                adc_reading += raw;
            }
        }
        adc_reading /= NO_OF_SAMPLES;
        //Convert adc_reading to voltage in mV
        uint32_t voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);
        //printf("Raw: %d\tVoltage: %dmV\n", adc_reading, voltage);

        


        if(voltage>=2200){ // si se recibe un voltaje en el adc mayor a 2.2 V
            Contador=Contador*20; //Calcula los milisegundos totales
            strcpy (cadena, "1\n");
            //Envia un dato a la cola para encender el led 
            if (xQueueSendToBack(cola_LED_recibido, &cadena,200/portTICK_RATE_MS)!=pdTRUE){//2seg--> Tiempo max. que la tarea está bloqueada si la cola está llena
                printf("error cola 1 LED\n");
            }
            //Envia los milisegundos a la cola para calcular la frecuencia y mostrarla
            if (xQueueSendToBack(cola_frecuencia, &Contador,200/portTICK_RATE_MS)!=pdTRUE){//2seg--> Tiempo max. que la tarea está bloqueada si la cola está llena
                printf("error cola frecuencia\n");
            }
            Contador=0; //Reinicia el contador para el siguiente periodo
        } else {
            Contador+=1;
        }

        vTaskDelay(pdMS_TO_TICKS(20)); //Se ejecuta cada 20 mS
    }

}


void LED_pulso (void* arg){     // Tarea 2 control del led amarillo cada vez que se recibe un impulso
    gpio_pad_select_gpio(13);
    gpio_set_direction(13,GPIO_MODE_OUTPUT);
    char Rx[3];
    gpio_set_level(13,0);
    while(1){
        if(xQueueReceive(cola_LED_recibido,&Rx,100/portTICK_RATE_MS)==pdTRUE) {//10s --> Tiempo max. que la tarea está bloqueada si la cola está vacía
            //printf("\n        PULSO RECIBIDO \n\n");
            gpio_set_level(13,0);
            vTaskDelay(1/portTICK_PERIOD_MS);
            gpio_set_level(13,1);
            vTaskDelay(50/portTICK_PERIOD_MS);
            gpio_set_level(13,0);
        }
    }

}


void calcular_frecuencia (void* arg){   // Tarea 3 Encargada del calculo de la frecuencia en latidos por minuto
    int cont;
    float res;
    char Cadena[3];
    while(1){
        if(xQueueReceive(cola_frecuencia,&cont,100/portTICK_RATE_MS)==pdTRUE) {//10s --> Tiempo max. que la tarea está bloqueada si la cola está vacía
            res=cont;
            res=res/1000;   //t en milisegundos a segundos 
            res=res/60;     //tiempo de segundos a minutos
            res=1/res;      //Frecuencia 1/T
            if(res>1 && res<150){
                printf("\n\n      FRECUENCIA: %f lpm",res);
                if (res>60 && res<100){
                    strcpy (Cadena, "1\n");
                    if (xQueueSendToBack(cola_bien_mal, &Cadena,200/portTICK_RATE_MS)!=pdTRUE){//2seg--> Tiempo max. que la tarea está bloqueada si la cola está llena
                        printf("error cola  LED bien o mal\n");
                    }

                } else{
                    strcpy (Cadena, "0\n");
                    if (xQueueSendToBack(cola_bien_mal, &Cadena,200/portTICK_RATE_MS)!=pdTRUE){//2seg--> Tiempo max. que la tarea está bloqueada si la cola está llena
                        printf("error cola  LED bien o mal\n");
                    }

                }

            }
        }
    }

}

void Normal_o_no (void* arg){   // Tarea 4 encargada del control de los leds verde o rojo segun lo recibido de la tarea 3
    gpio_pad_select_gpio(14);       // Configuracion LED verde
    gpio_set_direction(14,GPIO_MODE_OUTPUT);       

    gpio_pad_select_gpio(12);// Configuracion LED rojo
    gpio_set_direction(12,GPIO_MODE_OUTPUT);
    char Recibo[3];
    while(1){
        if(xQueueReceive(cola_bien_mal,&Recibo,100/portTICK_RATE_MS)==pdTRUE) {//10s --> Tiempo max. que la tarea está bloqueada si la cola está vacía
            if(strcmp(Recibo, "1\n") == 0){     // Si se recibe un 1 activa el led verde y apaga el rojo
                gpio_set_level(14,1);
                gpio_set_level(12,0);
            } else {    //si no activa el rojo
                gpio_set_level(12,1);
                gpio_set_level(14,0);
            }
            
        }
        
    }

}



void app_main(void)
{

    cola_LED_recibido = xQueueCreate(20, 3);
    cola_frecuencia = xQueueCreate(20, sizeof (int));
    cola_bien_mal = xQueueCreate(20, 3);

    xTaskCreate(receptorADC, "receptorADC", 1024*3, NULL, 5, NULL);
    xTaskCreate(LED_pulso, "LED_pulso", 1024*3, NULL, 3, NULL);
    xTaskCreate(calcular_frecuencia, "calcular_frecuencia", 1024*3, NULL, 2, NULL);
    xTaskCreate(Normal_o_no, "Normal_o_no", 1024*3, NULL, 1, NULL);
}
