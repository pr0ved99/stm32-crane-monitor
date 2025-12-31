# 🏗️ STM32 및 Vue.js 기반 타워 크레인 시뮬레이터 실시간 모니터링 시스템

> STM32 센서 데이터의 Node.js 미들웨어 및 Vue.js 클라이언트 연동을 통한 저지연(Low-latency) 웹 모니터링 파이프라인 구축

<br/>

## 🎥 시연 영상 (Demo Video)

[![시연 영상](http://img.youtube.com/vi/o8msN-PNJuo/0.jpg)](https://youtu.be/o8msN-PNJuo)

*(위 이미지를 클릭하면 유튜브 영상으로 이동합니다)*
<br/>

## 1. 프로젝트 개요 (Project Overview)

본 프로젝트는 산업용 타워 크레인 시뮬레이터에서 생성되는 센서 데이터를 웹 환경에서 실시간으로 시각화하기 위해 설계되었습니다. 단순한 데이터 전송을 넘어, **하드웨어(UART)에서 웹(WebSocket)으로 이어지는 전체 데이터 파이프라인의 지연 시간(Latency) 최소화** 및 전송 과정에서의 데이터 무결성 확보를 핵심 목표로 합니다.

### 🎯 핵심 기술 목표 (Key Technical Objectives)

- **저지연 데이터 전송 (Low-latency Transmission):** 115200bps의 UART 통신과 WebSocket 기술을 결합하여 실시간 모니터링 환경을 구현하였습니다.
- **데이터 무결성 보장 (Data Integrity Assurance):** 패킷 분할 처리를 통해 고속 전송 시 발생할 수 있는 데이터 파편화 및 병합 현상을 방지하였습니다.
- **시스템 리소스 최적화 (Resource Optimization):** 클라이언트 측의 메모리 누수를 방지하고 렌더링 성능을 유지하기 위한 메모리 관리 알고리즘을 적용하였습니다.

## 2. 시스템 아키텍처 (System Architecture)

본 시스템은 STM32(Firmware), Node.js(Middleware), Vue.js(Client)의 3계층 구조로 구성되어 있으며, 데이터 흐름은 실시간성 확보를 위해 단방향 비동기 통신 방식을 채택하였습니다.

### 📡 데이터 흐름도 (Data Flow)

```
+----------------+       UART (115200bps)      +------------------+     WebSocket (Socket.IO)     +------------------+
| STM32 F103RB   | --------------------------> | Node.js Middleware | --------------------------> | Vue.js Client      |
| (Firmware)     |       Serial Stream         | (Server)           |       Event: 'nucleoData'   | (Application)      |
+----------------+                             +------------------+                               +------------------+

```

| **구분 (Layer)** | **기술 스택 (Tech Stack)** | **역할 (Role)** |
|-----|---|---|
| **Firmware** | C, HAL Driver | 센서 데이터 수집 및 `sprintf` 기반의 시리얼 패킷 생성 |
| **Middleware** | Node.js, SerialPort | Serial-to-WebSocket 게이트웨이 및 데이터 파싱 수행 |
| **Frontend** | Vue.js 3 | 실시간 데이터 시각화 및 대시보드 렌더링 |

### 💻 펌웨어 핵심 구현 (Firmware Implementation)

**1. 데이터 수집 및 전송 프로토콜 (`main.c`)**
100ms 간격으로 조이스틱(ADC)과 버튼 상태를 스캔하고, Node.js 파서가 처리하기 쉽도록 **CSV 형태의 포맷**으로 직렬화하여 전송합니다. `HAL_GetTick()`을 활용한 Non-blocking 방식으로 구현하여 시스템 응답성을 확보했습니다.

```c
/* STM32 Main Loop (Non-blocking) */
while (1) {
    uint32_t now = HAL_GetTick();

    // 100ms 주기로 센서 데이터 스캔 및 전송
    if (now - t_ui >= 100) {
        t_ui = now;

        // 조이스틱(ADC) 및 버튼 상태 읽기 & 스케일링 (-5 ~ +5)
        int8_t Lx = (int8_t)scale0_10(ADC_ReadChannel(ADC_CHANNEL_0)) - 5;
        int8_t Ly = 5 - (int8_t)scale0_10(ADC_ReadChannel(ADC_CHANNEL_1));
        // ... (나머지 센서 읽기 생략)

        // UART 전송: printf 리디렉션을 통해 포맷팅된 문자열 전송
        // Protocol: "LX:-5,LY:0,LS:0,RX:0,RY:0,RS:0,UP:0,DN:0\n"
        printf("LX:%d,LY:%d,LS:%u,RX:%d,RY:%d,RS:%u,UP:%u,DN:%u\n",
               Lx, Ly, Ls, Rx, Ry, Rs, up_btn, down_btn);
    }
}
```

**2. UART 출력을 위한 printf 리디렉션 표준 출력 함수(printf)를 UART 핸들과 연결하여, 복잡한 문자열 처리를 간결하게 구현했습니다.**

```c
/* printf 함수 내부에서 호출되는 하위 레벨 함수 재정의 */
#ifdef __GNUC__
#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#endif

PUTCHAR_PROTOTYPE {
  // printf 호출 시 UART2로 문자 1바이트 전송
  HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, 0xFFFF);
  return ch;
}
```

**3. I2C LCD 드라이버 추상화 (LiquidCrystal_I2C.h) HAL 라이브러리에 의존하지 않고, 데이터시트를 기반으로 I2C 통신 로직을 직접 구현 및 모듈화하여 하드웨어 제어 효율을 높였습니다.**

```c
#ifndef LIQUIDCRYSTAL_I2C_H
#define LIQUIDCRYSTAL_I2C_H

#include <stdint.h>
#include "stm32f1xx_hal.h"

void LCD_Init(I2C_HandleTypeDef *hi2c, uint8_t address, uint8_t cols, uint8_t rows);
void LCD_Clear(void);
void LCD_SetCursor(uint8_t col, uint8_t row);
void LCD_WriteString(char *str);
void LCD_SendCommand(uint8_t cmd);
void LCD_SendData(uint8_t data);

#endif //LIQUIDCRYSTAL_I2C_H
```


## 3. 기술적 난제 및 해결 방안 (Technical Challenges & Solutions)

### 3.1. 시리얼 데이터 패킷의 파편화 및 병합 문제 (Middleware)

- **문제점:** STM32에서 고속으로 데이터를 전송할 경우, Node.js 수신부에서 패킷이 분할되거나 다수의 패킷이 병합되어 수신되는 현상이 발생하여 데이터 신뢰성이 저하되었습니다.
- **해결 방안:** `ReadlineParser`를 도입하여 개행 문자(`\n`)를 구분자(Delimiter)로 설정함으로써 스트림 데이터를 명확한 패킷 단위로 분할 처리하였습니다.
- **구현 코드:**
    
    ```
    // server.js
    const parser = port.pipe(new ReadlineParser({ delimiter: '\r\n' }));
    
    // 파싱된 온전한 데이터 패킷만을 클라이언트로 브로드캐스팅 수행
    parser.on('data', (data) => {
        io.emit('nucleoData', data);
    });
    
    ```
    

### 3.2. 클라이언트 메모리 부하 및 렌더링 성능 저하 (Frontend)

- **문제점:** 장시간 모니터링 수행 시, 누적되는 로그 데이터로 인해 브라우저의 메모리 사용량이 증가하고 DOM 렌더링 성능이 저하되는 문제가 식별되었습니다.
- **해결 방안:** **FIFO(First-In-First-Out)** 방식의 원형 큐(Circular Queue) 개념을 적용하여, 데이터 배열의 길이를 10개로 제한함으로써 메모리 사용량을 일정 수준으로 유지하였습니다.
- **구현 코드:**
    
    ```
    // Vue Component Logic
    const updateLog = (newData) => {
        logs.value.push(newData);
        // 배열 길이가 임계값을 초과할 경우 가장 오래된 데이터 소거
        if (logs.value.length > 10) {
            logs.value.shift();
        }
    };
    
    ```
    

### 3.3. 크로스 플랫폼 환경에서의 포트 호환성 문제

- **문제점:** 개발 환경(Windows, `COM3`)과 배포 환경(Linux, `/dev/ttyUSB0`) 간의 시리얼 포트 경로 차이로 인해 코드의 이식성이 제한되었습니다.
- **해결 방안:** 환경 변수(Environment Variables) 또는 외부 설정 파일을 통해 포트 경로를 주입받도록 구조를 개선하여, 운영체제에 종속되지 않는 유연한 연결성을 확보하였습니다.

## 4. 설치 및 실행 가이드 (Installation & Execution)

### 사전 요구 사항 (Prerequisites)

- Node.js (v14.0.0 이상)
- STM32 개발 보드 (USB 연결 필요)

### 1. 미들웨어 서버 구동 (Middleware Setup)

```
cd server
npm install
node server.js

```

### 2. 클라이언트 애플리케이션 실행 (Frontend Setup)

```
cd client
npm install
npm run dev

```

## 5. 라이선스 (License)

본 프로젝트는 MIT 라이선스 하에 배포됩니다.
