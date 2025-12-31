<template>
  <div class="simulation-container">
    <div ref="canvasContainer" class="canvas-container"></div>
    <div class="info-panel">
      <h3>🔌 크레인 제어</h3>
      <div class="status">
        상태:
        <span :class="['status-pill', isConnected ? 'connected' : 'disconnected']">
          {{ isConnected ? '연결됨' : '연결 안됨' }}
        </span>
      </div>
      <p>서버로부터 실시간 제어 데이터를 수신하여 3D 시뮬레이션을 렌더링합니다.</p>
      <div class="log-container">
        <h4>수신 로그</h4>
        <pre ref="logEl">{{ logContent }}</pre>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted, nextTick, reactive, watch } from 'vue'
import { io } from 'socket.io-client'
import * as THREE from 'three'

// --- 뷰 관련 Refs ---
const canvasContainer = ref(null)
const logEl = ref(null)
const isConnected = ref(false)
const logContent = ref('')
let socket

// --- Three.js 관련 변수 ---
let scene, camera, renderer, clock
let crane, jib, trolley, hookGroup, gripper, gripperLeft, gripperRight
let constructionMaterials = []
let grabbedMaterial = null
let groundTarget = null // 집게 위치 표시용 마커

// --- 상태 관리 ---

// NUCLEO 보드에서 직접 받는 원본 제어 데이터
const controls = reactive({
  LX: 0,
  LY: 0,
  LS: 0,
  RX: 0,
  RY: 0,
  RS: 0, // RS 기본값 0으로 변경
  UP: 0,
  DN: 0,
})

// 클라이언트에서 계산되는 3D 객체의 실제 상태
const craneState = reactive({
  jibRotation: 0,
  trolleyPosition: 2, // 초기 위치
  hookHeight: 15,
  gripperIsOpen: true,
})

const cameraState = reactive({
  distance: 40,
  polar: 60,
  azimuth: 45,
  presetIndex: 0, // 현재 카메라 프리셋 인덱스
})

const cameraPresets = [
  { distance: 40, polar: 60, azimuth: 45 }, // 기본
  { distance: 50, polar: 90, azimuth: 0 }, // 측면
  { distance: 50, polar: 10, azimuth: 0 }, // 위에서
  { distance: 40, polar: 70, azimuth: 180 }, // 후면
]

// [FIX] 속도 계수 하향 조정
const SPEED_FACTORS = {
  JIB_ROTATE: 2, // RY (4 -> 2)
  TROLLEY_MOVE: 6, // RX (10 -> 6)
  HOOK_MOVE: 5, // UP/DN (8 -> 5)
  CAM_AZIMUTH: 25, // LX
  CAM_POLAR: 25, // LY
}

// --- Three.js 초기화 및 모델 생성 ---

const initThree = () => {
  scene = new THREE.Scene()
  scene.background = new THREE.Color(0x87ceeb)
  clock = new THREE.Clock()
  renderer = new THREE.WebGLRenderer({ antialias: true })
  renderer.setSize(canvasContainer.value.clientWidth, canvasContainer.value.clientHeight)
  renderer.shadowMap.enabled = true
  canvasContainer.value.appendChild(renderer.domElement)
  camera = new THREE.PerspectiveCamera(
    75,
    canvasContainer.value.clientWidth / canvasContainer.value.clientHeight,
    0.1,
    1000,
  )

  const ambientLight = new THREE.AmbientLight(0xffffff, 0.6)
  scene.add(ambientLight)
  const directionalLight = new THREE.DirectionalLight(0xffffff, 0.8)
  directionalLight.position.set(-15, 25, 20)
  directionalLight.castShadow = true
  scene.add(directionalLight)
  const groundGeometry = new THREE.PlaneGeometry(100, 100)
  const groundMaterial = new THREE.MeshStandardMaterial({ color: 0x996633 })
  const ground = new THREE.Mesh(groundGeometry, groundMaterial)
  ground.rotation.x = -Math.PI / 2
  ground.receiveShadow = true
  scene.add(ground)

  createCrane()
  createMaterials()
  createGroundTarget() // 빨간 점 생성
  animate()
  window.addEventListener('resize', onWindowResize)
}

const createCrane = () => {
  crane = new THREE.Group()
  const mainMaterial = new THREE.MeshStandardMaterial({ color: 0xffde00 })
  const base = new THREE.Mesh(
    new THREE.CylinderGeometry(2, 2.5, 1, 32),
    new THREE.MeshStandardMaterial({ color: 0x555555 }),
  )
  base.position.y = 0.5
  base.castShadow = true
  crane.add(base)
  const mast = new THREE.Mesh(new THREE.BoxGeometry(1.5, 20, 1.5), mainMaterial)
  mast.position.y = 11
  mast.castShadow = true
  crane.add(mast)
  jib = new THREE.Group()
  jib.position.y = 21
  const mainJib = new THREE.Mesh(new THREE.BoxGeometry(15, 1, 1), mainMaterial)
  mainJib.position.x = 7.5
  mainJib.castShadow = true
  jib.add(mainJib)
  const counterJib = new THREE.Mesh(new THREE.BoxGeometry(5, 1, 1), mainMaterial)
  counterJib.position.x = -2.5
  counterJib.castShadow = true
  jib.add(counterJib)
  const counterWeight = new THREE.Mesh(
    new THREE.BoxGeometry(2, 2.5, 2.5),
    new THREE.MeshStandardMaterial({ color: 0x333333 }),
  )
  counterWeight.position.set(-4, -0.75, 0)
  counterWeight.castShadow = true
  jib.add(counterWeight)
  crane.add(jib)
  trolley = new THREE.Mesh(
    new THREE.BoxGeometry(1, 0.5, 1),
    new THREE.MeshStandardMaterial({ color: 0xff0000 }),
  )
  trolley.position.set(2, -0.75, 0)
  trolley.castShadow = true
  jib.add(trolley)
  hookGroup = new THREE.Group()
  const cableMaterial = new THREE.LineBasicMaterial({ color: 0x000000 })
  const cablePoints = [new THREE.Vector3(0, 0, 0), new THREE.Vector3(0, -15, 0)]
  const cableGeometry = new THREE.BufferGeometry().setFromPoints(cablePoints)
  const cable = new THREE.Line(cableGeometry, cableMaterial)
  hookGroup.add(cable)
  gripper = new THREE.Group()
  gripper.position.y = -15
  const gripperMaterial = new THREE.MeshStandardMaterial({ color: 0x444444, metalness: 0.8 })
  gripperLeft = new THREE.Mesh(new THREE.BoxGeometry(0.2, 0.8, 0.2), gripperMaterial)
  gripperLeft.position.x = -0.3
  gripperRight = new THREE.Mesh(new THREE.BoxGeometry(0.2, 0.8, 0.2), gripperMaterial)
  gripperRight.position.x = 0.3
  gripper.add(gripperLeft, gripperRight)
  hookGroup.add(gripper)
  trolley.add(hookGroup)
  scene.add(crane)
}

const createMaterials = () => {
  const materialGeometry = new THREE.BoxGeometry(2, 2, 2)
  const colors = [0x0099ff, 0x00ff99, 0xff9900]
  const positions = [
    new THREE.Vector3(10, 1, 5),
    new THREE.Vector3(5, 1, -10),
    new THREE.Vector3(-8, 1, -8),
  ]
  for (let i = 0; i < 3; i++) {
    const material = new THREE.Mesh(
      materialGeometry,
      new THREE.MeshStandardMaterial({ color: colors[i] }),
    )
    material.position.copy(positions[i])
    material.castShadow = true
    material.name = `material_${i}`
    scene.add(material)
    constructionMaterials.push(material)
  }
}

const createGroundTarget = () => {
  const targetGeometry = new THREE.CircleGeometry(0.5, 32)
  // 재질을 MeshBasicMaterial로 하여 조명에 영향을 받지 않도록 함
  const targetMaterial = new THREE.MeshBasicMaterial({ color: 0xff0000 })
  groundTarget = new THREE.Mesh(targetGeometry, targetMaterial)
  groundTarget.rotation.x = -Math.PI / 2 // 바닥에 평평하게 눕힘
  groundTarget.position.y = 0.01 // Z-fighting 방지를 위해 살짝 띄움
  scene.add(groundTarget)
}

// --- 로직 및 애니메이션 ---

const handleGripperAction = (shouldBeOpen) => {
  if (shouldBeOpen) {
    if (grabbedMaterial) {
      const worldPosition = new THREE.Vector3()
      grabbedMaterial.getWorldPosition(worldPosition)
      scene.add(grabbedMaterial)
      grabbedMaterial.position.copy(worldPosition)
      grabbedMaterial.position.y = 1
      grabbedMaterial = null
    }
  } else {
    if (!grabbedMaterial) {
      const gripperWorldPos = new THREE.Vector3()
      gripper.getWorldPosition(gripperWorldPos)
      for (const material of constructionMaterials) {
        if (material.parent !== scene) continue
        if (gripperWorldPos.distanceTo(material.position) < 2.0 && gripperWorldPos.y < 2.5) {
          grabbedMaterial = material
          gripper.add(grabbedMaterial)
          grabbedMaterial.position.set(0, -1, 0)
          grabbedMaterial.rotation.set(0, 0, 0)
          break
        }
      }
    }
  }
}

function updateStateFromControls(delta) {
  if (!isConnected.value) return

  // 크레인 상태 업데이트 (조이스틱 값 정규화: -1 ~ 1 범위로 가정)
  craneState.jibRotation += (controls.RY / 5.0) * delta * SPEED_FACTORS.JIB_ROTATE
  craneState.trolleyPosition += (controls.RX / 5.0) * delta * SPEED_FACTORS.TROLLEY_MOVE

  if (controls.UP === 1) {
    craneState.hookHeight += delta * SPEED_FACTORS.HOOK_MOVE
  }
  if (controls.DN === 1) {
    craneState.hookHeight -= delta * SPEED_FACTORS.HOOK_MOVE
  }

  craneState.trolleyPosition = Math.max(0.5, Math.min(14.5, craneState.trolleyPosition))
  craneState.hookHeight = Math.max(2, Math.min(20, craneState.hookHeight))

  // 카메라 상태 업데이트 (조이스틱 값 정규화)
  cameraState.azimuth += (controls.LX / 5.0) * delta * SPEED_FACTORS.CAM_AZIMUTH
  cameraState.polar -= (controls.LY / 5.0) * delta * SPEED_FACTORS.CAM_POLAR
  cameraState.polar = Math.max(10, Math.min(170, cameraState.polar))
}

function applyStateToObjects() {
  if (jib) jib.rotation.y = craneState.jibRotation
  if (trolley) trolley.position.x = craneState.trolleyPosition

  if (gripper) gripper.position.y = -craneState.hookHeight
  if (hookGroup) {
    const cable = hookGroup.children[0]
    const positions = cable.geometry.attributes.position.array
    positions[4] = -craneState.hookHeight
    cable.geometry.attributes.position.needsUpdate = true
  }

  if (gripperLeft && gripperRight) {
    const gripperX = craneState.gripperIsOpen ? 0.3 : 0.1
    gripperLeft.position.x = -gripperX
    gripperRight.position.x = gripperX
  }

  // [ADD] 지상 타겟 마커 위치 업데이트
  if (groundTarget && gripper) {
    const gripperWorldPos = new THREE.Vector3()
    gripper.getWorldPosition(gripperWorldPos)
    groundTarget.position.x = gripperWorldPos.x
    groundTarget.position.z = gripperWorldPos.z
  }

  const { distance, polar, azimuth } = cameraState
  const polarRad = THREE.MathUtils.degToRad(polar)
  const azimuthRad = THREE.MathUtils.degToRad(azimuth)
  camera.position.x = distance * Math.sin(polarRad) * Math.cos(azimuthRad)
  camera.position.y = distance * Math.cos(polarRad) + 10
  camera.position.z = distance * Math.sin(polarRad) * Math.sin(azimuthRad)
  camera.lookAt(0, 10, 0)
}

const animate = () => {
  requestAnimationFrame(animate)
  const delta = clock.getDelta()
  updateStateFromControls(delta)
  applyStateToObjects()
  renderer.render(scene, camera)
}

const appendLog = (line) => {
  logContent.value += line + '\n'
  nextTick(() => {
    if (logEl.value) logEl.value.scrollTop = logEl.value.scrollHeight
  })
}

const onWindowResize = () => {
  if (camera && renderer && canvasContainer.value) {
    camera.aspect = canvasContainer.value.clientWidth / canvasContainer.value.clientHeight
    camera.updateProjectionMatrix()
    renderer.setSize(canvasContainer.value.clientWidth, canvasContainer.value.clientHeight)
  }
}

// --- 생명주기 및 소켓 핸들러 ---

onMounted(() => {
  initThree()
  socket = io('http://localhost:3000')

  socket.on('connect', () => {
    isConnected.value = true
    appendLog('Server connected.')
  })
  socket.on('disconnect', () => {
    isConnected.value = false
    appendLog('Server disconnected.')
  })

  socket.on('serial-data', (data) => {
    appendLog(`RX: ${data}`)
    try {
      data.split(',').forEach((pair) => {
        const parts = pair.split(':')
        if (parts.length === 2) {
          const key = parts[0].trim().toUpperCase()
          const value = parseInt(parts[1].trim(), 10)
          if (key in controls) {
            controls[key] = isNaN(value) ? 0 : value
          }
        }
      })
    } catch (e) {
      // 파싱 실패 시 무시
    }
  })
})

onUnmounted(() => {
  if (socket) socket.disconnect()
  window.removeEventListener('resize', onWindowResize)
})

watch(
  () => controls.RS,
  (newValue, oldValue) => {
    // [FIX] RS 버튼이 눌렸을 때 (값이 0에서 1로 바뀔 때) 집게 상태를 토글합니다.
    if (newValue === 1 && oldValue === 0) {
      craneState.gripperIsOpen = !craneState.gripperIsOpen
      handleGripperAction(craneState.gripperIsOpen)
    }
  },
)

watch(
  () => controls.LS,
  (newValue, oldValue) => {
    // LS 버튼이 눌렸을 때 (값이 0에서 1로 바뀔 때) 카메라 위치를 순환시킵니다.
    if (newValue === 1 && oldValue === 0) {
      cameraState.presetIndex = (cameraState.presetIndex + 1) % cameraPresets.length
      const preset = cameraPresets[cameraState.presetIndex]
      if (preset) {
        cameraState.distance = preset.distance
        cameraState.polar = preset.polar
        cameraState.azimuth = preset.azimuth
      }
    }
  },
)
</script>

<style scoped>
.simulation-container {
  display: flex;
  width: 100vw;
  height: 100vh;
  background-color: #333;
  font-family: 'Helvetica', 'Arial', sans-serif;
  color: white;
  position: relative;
}
.canvas-container {
  flex-grow: 1;
  height: 100%;
  overflow: hidden;
}
.info-panel {
  position: absolute;
  top: 20px;
  left: 20px;
  width: 320px;
  background-color: rgba(0, 0, 0, 0.75);
  padding: 15px;
  border-radius: 8px;
  box-sizing: border-box;
  display: flex;
  flex-direction: column;
  gap: 10px;
}
.info-panel h3 {
  margin: 0;
  color: #fdd835;
  border-bottom: 1px solid #fdd835;
  padding-bottom: 10px;
}
.info-panel p {
  font-size: 14px;
  line-height: 1.5;
  margin: 5px 0;
}
.status {
  font-size: 14px;
  display: flex;
  align-items: center;
  gap: 8px;
}
.status-pill {
  padding: 4px 10px;
  border-radius: 999px;
  font-weight: 600;
  font-size: 12px;
}
.status-pill.disconnected {
  background-color: #757575;
  color: white;
}
.status-pill.connected {
  background-color: #4caf50;
  color: white;
}
.log-container {
  margin-top: 10px;
}
.log-container h4 {
  margin: 0 0 5px 0;
  font-size: 14px;
  color: #ccc;
}
pre {
  background-color: rgba(0, 0, 0, 0.5);
  padding: 10px;
  border-radius: 5px;
  font-family: 'Courier New', Courier, monospace;
  font-size: 12px;
  white-space: pre-wrap;
  word-wrap: break-word;
  margin: 0;
  height: 200px;
  overflow-y: auto;
  border: 1px solid #444;
}
</style>
