# 

<h1 align="center">
  <br>
    <img width="200px" src="https://github.com/Ukobir/pwmControl/blob/main/imagens/logo.png">
  <br>
  Instrumento de Controle de PWM utilizando WI-FI com MQTT
  <br>
</h1>

## Descrição

Neste trabalho, foi desenvolvido um instrumento de controle de PWM utilizando o protocolo MQTT como cliente via Wi-Fi na placa Bitdoglab. Este equipamento pode ser empregado no controle de diversos dispositivos que utilizam o periférico PWM como método de acionamento, tais como motores, lâmpadas, LEDs, braços robóticos, entre outros.
O instrumento desenvolvido permite configurar um PWM na faixa de frequência entre 7,5 Hz e 62,5 MHz (a mesma faixa suportada pelo microcontrolador RP2040). Para isso, é necessário configurar um broker MQTT, como o Mosquitto, e utilizar um aplicativo cliente MQTT para se inscrever (publicar as configurações do PWM, como wrap e divisor).

### Pré-requisitos

1. **Git**: Certifique-se de ter o Git instalado no seu sistema. 
2. **VS Code**: Instale o Visual Studio Code, um editor de código recomendado para desenvolvimento com o Raspberry Pi Pico.
3. **Pico SDK**: Baixe e configure o SDK do Raspberry Pi Pico, conforme as instruções da documentação oficial.
4. Tenha acesso a placa **Bitdoglab**.

### Passos para Execução

1. **Clonar o repositório**: Clone o repositório utilizando o comando Git no terminal:
   
   ```bash
   https://github.com/Ukobir/pwmControl.git
   ```
2. Importar o código no VS Code com a extensão do Raspberry pi pico.
3. Compilar o código e carregar o código na **BitDogLab**.

## Testes Realizados
Foi feito diversos testes para garantir a funcionamento devido da atividade. Além de que foi organizado o código conforme explicado em aula.

## Vídeo de Demonstração
[Link do Vídeo](https://drive.google.com/file/d/1mLqbwV_QSHqHiQiAZonEo_Yq8I0PGf7m/view?usp=drive_link)


