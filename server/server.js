const express = require('express');
const http = require('http');
const { Server } = require("socket.io");
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');

// --- 설정 ---
const HTTP_PORT = 3000;
// ⭐️ 중요: 장치 관리자에서 확인한 STM32의 COM 포트 번호
const SERIAL_PORT_PATH = 'COM4'; // 예: 'COM4'
const BAUDRATE = 115200;

// Express, HTTP 서버 설정
const app = express();
const server = http.createServer(app);

// ⭐️ FIX: Socket.IO 서버에 CORS 설정 추가
const io = new Server(server, {
  cors: {
    origin: "*", // 모든 출처의 요청을 허용 (Vue 개발 서버 등)
    methods: ["GET", "POST"]
  }
});

// 시리얼 포트 설정
const port = new SerialPort({ path: SERIAL_PORT_PATH, baudRate: BAUDRATE });
const parser = port.pipe(new ReadlineParser({ delimiter: '\n' }));

// 서버 시작
server.listen(HTTP_PORT, () => {
    console.log(`웹 서버가 http://localhost:${HTTP_PORT} 에서 실행 중입니다.`);
});

// 시리얼 포트 에러 처리
port.on('error', (err) => {
    console.error('시리얼 포트 오류:', err.message);
    console.log(`\n팁: '${SERIAL_PORT_PATH}'가 올바른 COM 포트인지, 다른 프로그램이 사용 중이지 않은지 확인하세요.`);
});


// ⭐️ FIX: STM32로부터 데이터 수신 시, 연결된 클라이언트에게 바로 전송
parser.on('data', (data) => {
    const line = data.toString().trim();
    if (!line) return;
    
    console.log('STM32 -> Server:', line);
    // 'serial-data' 라는 이벤트 이름으로 모든 클라이언트에게 데이터 방송(broadcast)
    io.emit('serial-data', line);
});

// 웹 클라이언트 연결 처리
io.on('connection', (socket) => {
    console.log('웹 클라이언트가 연결되었습니다. ID:', socket.id);

    // 클라이언트로부터 'send-json' 메시지를 받았을 때 STM32로 전송
    socket.on('send-json', (jsonData) => {
        let text = jsonData.trim();
        if (!text) return;
        if (!text.endsWith('\n')) text += '\n';

        port.write(text, (err) => {
            if (err) {
                return console.error('Server -> STM32 전송 실패:', err.message);
            }
            console.log('Server -> STM32 전송:', text.trim());
        });
    });

    socket.on('disconnect', () => {
        console.log('웹 클라이언트 연결이 끊겼습니다. ID:', socket.id);
    });
});