# BNO055 I2C BUSY 문제 해결 기록

대상 펌웨어 경로:

```text
C:\Users\SSAFY\Desktop\ver3_0518_imu_recovery\firmware_05161929_update_pin
```

수정한 주요 파일:

```text
Src/main.c
Src/imu_bno055.c
```

## 1. 문제 상황

STM32F446RE 펌웨어에서 BNO055 IMU를 I2C1로 초기화하는 과정이 계속 실패했다.

초기 증상은 다음과 같았다.

```text
[BOOT] BNO055 IMUPLUS init...
[BNO055] no ACK at 0x28/0x29
[BOOT] BNO055 init failed
```

처음에는 단순히 BNO055가 I2C 주소 `0x28` 또는 `0x29`에서 ACK를 주지 않는 상황처럼 보였다. 그래서 가능한 원인은 다음으로 예상했다.

- BNO055 실제 배선이 현재 펌웨어의 I2C 핀과 다름
- SCL/SDA가 뒤바뀜
- BNO055 전원 또는 GND 문제
- I2C pull-up 문제
- 400 kHz I2C 속도가 불안정함
- BNO055가 아직 부팅되지 않았거나 reset 상태가 애매함

하지만 이후 `HAL_I2C_IsDeviceReady()`의 반환값을 직접 확인하면서, 단순한 주소 NACK 문제가 아니라는 점이 확인되었다.

## 2. 펌웨어 폴더 확인

작업 대상은 `ver3_0518_imu_recovery` 아래의 다음 폴더였다.

```text
C:\Users\SSAFY\Desktop\ver3_0518\firmware_05161929_update_pin
```

처음 확인했을 때 `Debug/ST.list`에는 `bno055_init_imuplus(&hi2c1)` 호출 흔적이 있었지만, 실제 소스인 `Src/main.c`에는 해당 호출이 빠져 있었다. 즉 빌드 산출물과 현재 소스가 일치하지 않는 상태였다.

이 문제는 CubeMX 또는 CubeIDE Generate Code 과정에서 `main.c`가 다시 생성되며 사용자 코드 일부가 빠진 것으로 판단했다.

CubeMX가 다시 생성해도 유지되도록, 커스텀 코드는 `USER CODE` 보호 블록 안에 넣었다.

## 3. main.c 수정

`Src/main.c`의 include 영역에 다음을 추가했다.

```c
/* USER CODE BEGIN Includes */
#include "control_loop.h"
#include "imu_bno055.h"
#include <stdio.h>
/* USER CODE END Includes */
```

그리고 peripheral 초기화가 끝난 뒤, `control_loop_run()`에 들어가기 전에 BNO055 초기화를 수행하도록 했다.

```c
/* USER CODE BEGIN 2 */
printf("\r\n[BOOT] BNO055 IMUPLUS init...\r\n");
if (!bno055_init_imuplus(&hi2c1)) {
  printf("[BOOT] BNO055 init failed\r\n");
  Error_Handler();
}
printf("[BOOT] BNO055 init OK\r\n");

control_loop_run();
/* USER CODE END 2 */
```

이 위치를 선택한 이유는 다음과 같다.

- `MX_I2C1_Init()` 이후라서 `hi2c1`이 초기화된 상태
- `control_loop_run()` 진입 전이라서 IMU 초기화 실패를 boot 단계에서 확실히 잡을 수 있음
- `USER CODE BEGIN 2` 안이라 CubeMX Generate Code 후에도 유지됨

## 4. 첫 번째 진단: HAL 반환값 확인

처음에는 `bno055_init_imuplus()`가 단순히 `false`만 반환했기 때문에 실패 지점을 알 수 없었다.

그래서 `Src/imu_bno055.c`에 `HAL_I2C_IsDeviceReady()`의 반환값을 출력하는 로그를 추가했다.

```c
static const char *hal_status_name(HAL_StatusTypeDef status) {
    switch (status) {
        case HAL_OK:      return "HAL_OK";
        case HAL_ERROR:   return "HAL_ERROR";
        case HAL_BUSY:    return "HAL_BUSY";
        case HAL_TIMEOUT: return "HAL_TIMEOUT";
        default:          return "HAL_UNKNOWN";
    }
}
```

그리고 `0x28`, `0x29` 각각에 대해 상태와 `ErrorCode`를 출력했다.

```c
HAL_StatusTypeDef ready28 =
    HAL_I2C_IsDeviceReady(hi2c, BNO055_I2C_ADDR_DEFAULT << 1, 3, 50);
uint32_t err28 = HAL_I2C_GetError(hi2c);
printf("[BNO055] IsDeviceReady 0x28 -> %s (%d), ErrorCode=0x%08lX\r\n",
       hal_status_name(ready28), (int)ready28, (unsigned long)err28);
```

그 결과 `HAL_ERROR`가 아니라 `HAL_BUSY`가 반환되는 것을 확인했다.

`HAL_StatusTypeDef` 값의 의미는 다음과 같다.

```text
HAL_OK      = 0
HAL_ERROR   = 1
HAL_BUSY    = 2
HAL_TIMEOUT = 3
```

즉 단순히 BNO055가 주소 ACK를 안 주는 문제가 아니라, STM32 HAL/I2C peripheral이 이미 bus busy 상태라고 판단하고 있었다.

## 5. 두 번째 진단: HAL State와 I2C peripheral flag 확인

`HAL_BUSY`의 원인을 더 좁히기 위해 다음 진단 로그를 추가했다.

```c
static void bno_print_i2c_diag(I2C_HandleTypeDef *hi2c, const char *tag) {
    printf("[I2C %s] State=0x%02X Lock=0x%02X Error=0x%08lX BUSY=%lu SR1=0x%04lX SR2=0x%04lX\r\n",
           tag,
           (unsigned int)hi2c->State,
           (unsigned int)hi2c->Lock,
           (unsigned long)HAL_I2C_GetError(hi2c),
           (unsigned long)(__HAL_I2C_GET_FLAG(hi2c, I2C_FLAG_BUSY) ? 1UL : 0UL),
           (unsigned long)hi2c->Instance->SR1,
           (unsigned long)hi2c->Instance->SR2);
}
```

실제 출력은 다음과 같았다.

```text
[I2C before] State=0x20 Lock=0x00 Error=0x00000000 BUSY=1 SR1=0x0000 SR2=0x0002
[I2C after 0x28] State=0x20 Lock=0x00 Error=0x00000020 BUSY=1 SR1=0x0000 SR2=0x0002
[BNO055] IsDeviceReady 0x28 -> HAL_BUSY (2), ErrorCode=0x00000020
[I2C after 0x29] State=0x20 Lock=0x00 Error=0x00000020 BUSY=1 SR1=0x0000 SR2=0x0002
[BNO055] IsDeviceReady 0x29 -> HAL_BUSY (2), ErrorCode=0x00000020
```

이 로그를 해석하면 다음과 같다.

- `State=0x20`: HAL I2C state는 `HAL_I2C_STATE_READY`
- `Lock=0x00`: HAL lock은 걸려 있지 않음
- `SR1=0x0000`: ADDR, AF, BERR 같은 이벤트는 없음
- `SR2=0x0002`: STM32 I2C peripheral의 `BUSY` bit가 set
- `BUSY=1`: 주소 전송 전부터 bus busy 상태

따라서 원인은 `hi2c1.State`가 READY가 아닌 HAL 내부 상태 문제가 아니었다.

진짜 문제는 STM32 I2C peripheral이 부팅 직후부터 I2C bus가 busy라고 판단하는 것이었다.

즉 다음 가능성이 남았다.

- SDA 또는 SCL 라인이 LOW로 잡혀 있음
- BNO055가 SDA를 release하지 못함
- 이전 I2C transaction이 STOP 없이 끊긴 것처럼 bus가 stuck됨
- BNO055 전원/리셋 상태가 애매해 I2C line을 잡고 있음
- STM32F4 I2C peripheral의 BUSY stuck

## 6. 세 번째 진단: SCL/SDA 라인 레벨 확인

BUSY flag가 set인 상태에서, I2C peripheral을 DeInit하고 PB8/PB9를 GPIO open-drain으로 바꾼 뒤 핀 레벨을 읽도록 했다.

현재 펌웨어의 I2C 핀은 다음과 같다.

```text
PB8 = I2C1_SCL = BNO055 SCL
PB9 = I2C1_SDA = BNO055 SDA
```

복구 코드의 핵심은 다음이다.

```c
HAL_I2C_DeInit(hi2c);
__HAL_RCC_I2C1_FORCE_RESET();
__HAL_RCC_I2C1_RELEASE_RESET();

GPIO_InitTypeDef gp = {0};
gp.Pin = GPIO_PIN_8 | GPIO_PIN_9;
gp.Mode = GPIO_MODE_OUTPUT_OD;
gp.Pull = GPIO_PULLUP;
gp.Speed = GPIO_SPEED_FREQ_LOW;
HAL_GPIO_Init(GPIOB, &gp);

HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_SET);
HAL_Delay(2);
printf("[I2C recover] idle pins SCL=%d SDA=%d\r\n",
       (int)HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8),
       (int)HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9));
```

실제 출력은 다음과 같았다.

```text
[I2C recover] idle pins SCL=1 SDA=0
```

이 한 줄로 원인이 거의 확정되었다.

SCL은 HIGH였지만 SDA가 LOW였다. 즉 부팅 직후 BNO055 또는 I2C bus 쪽에서 SDA를 잡고 있었고, STM32는 이를 보고 bus busy로 판단했다.

## 7. 해결: I2C bus recovery

I2C 표준 bus recovery 방식에 맞춰 SCL을 9번 토글하고 STOP condition을 만들어 SDA release를 유도했다.

구현한 복구 순서는 다음과 같다.

1. `HAL_I2C_DeInit()`
2. I2C1 peripheral reset
3. PB8/PB9를 GPIO open-drain output으로 설정
4. SCL/SDA를 HIGH로 release
5. SCL을 9번 토글
6. SDA를 LOW로 내렸다가 SCL HIGH 상태에서 SDA HIGH로 올려 STOP condition 생성
7. `HAL_I2C_Init()`으로 I2C1 재초기화

구현 코드:

```c
for (int i = 0; i < 9; i++) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
    HAL_Delay(1);
}

HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET);
HAL_Delay(1);
HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
HAL_Delay(1);
HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET);
HAL_Delay(2);
```

복구 후 로그:

```text
[I2C recover] after clocks SCL=1 SDA=1
[I2C after recovery] State=0x20 Lock=0x00 Error=0x00000000 BUSY=0 SR1=0x0000 SR2=0x0000
```

이제 SCL/SDA 둘 다 HIGH가 되었고, STM32 I2C peripheral의 BUSY flag도 clear되었다.

## 8. 최종 성공 로그

bus recovery 후 BNO055 초기화가 정상 통과했다.

```text
[BOOT] BNO055 IMUPLUS init...
[I2C before] State=0x20 Lock=0x00 Error=0x00000000 BUSY=1 SR1=0x0000 SR2=0x0002
[I2C recover] start
[I2C recover] idle pins SCL=1 SDA=0
[I2C recover] after clocks SCL=1 SDA=1
[I2C after recovery] State=0x20 Lock=0x00 Error=0x00000000 BUSY=0 SR1=0x0000 SR2=0x0000
[I2C after 0x28] State=0x20 Lock=0x00 Error=0x00000000 BUSY=0 SR1=0x0000 SR2=0x0000
[BNO055] IsDeviceReady 0x28 -> HAL_OK (0), ErrorCode=0x00000000
[BNO055] addr=0x28 ready
[BNO055] CHIP_ID OK
[BOOT] BNO055 init OK

=== Control loop started (50Hz, UART) ===
```

이 로그로 확인된 사실:

- BNO055 실제 주소는 `0x28`
- I2C wiring 자체는 맞음
- BNO055 `CHIP_ID`는 정상
- 문제는 주소나 chip id가 아니라, boot 직후 SDA stuck low로 인한 I2C BUSY 상태였음
- SCL clock recovery와 STOP condition 생성으로 복구 가능함

## 9. 최종 원인

최종 원인은 다음으로 정리할 수 있다.

```text
부팅 직후 BNO055/I2C bus에서 SDA가 LOW로 stuck되어,
STM32F4 I2C peripheral이 BUSY flag를 set한 상태로 시작했다.
이 때문에 HAL_I2C_IsDeviceReady()는 주소를 보내기도 전에 HAL_BUSY를 반환했다.
SCL 9회 토글 + STOP condition 기반 I2C bus recovery를 적용하자 SDA가 release되었고,
BNO055가 정상적으로 0x28에서 ACK 및 CHIP_ID 응답을 했다.
```

## 10. 면접/포트폴리오용 설명 포인트

이 이슈는 단순히 "센서가 안 붙는다" 문제가 아니라, HAL layer, peripheral register, 실제 bus line 상태를 단계적으로 분리해서 해결한 사례다.

문제 해결 과정은 다음 구조로 설명할 수 있다.

1. `bno055_init_imuplus()` 실패 현상 확인
2. `HAL_I2C_IsDeviceReady()` 반환값을 출력해 `HAL_BUSY`임을 확인
3. `hi2c1.State`, `Lock`, `ErrorCode`, `SR1`, `SR2`, `I2C_FLAG_BUSY`를 출력해 HAL state 문제와 bus busy 문제를 분리
4. `State=READY`이지만 `SR2.BUSY=1`임을 확인
5. I2C peripheral을 GPIO로 전환해 실제 SCL/SDA line level을 읽음
6. `SCL=1`, `SDA=0`으로 SDA stuck low를 확인
7. I2C bus recovery 방식으로 SCL 9회 토글 및 STOP condition 생성
8. SDA release 및 BUSY flag clear 확인
9. BNO055 `0x28` ACK와 `CHIP_ID=0xA0` 확인
10. control loop 정상 진입 확인

이 과정에서 중요한 기술적 포인트는 다음이다.

- `HAL_BUSY`는 단순히 slave ACK 실패가 아니다.
- `HAL_I2C_STATE_READY`와 peripheral `I2C_FLAG_BUSY`는 별개로 봐야 한다.
- STM32F4 I2C peripheral은 bus line 상태에 따라 초기화 직후에도 BUSY stuck이 발생할 수 있다.
- I2C는 open-drain bus이므로 idle 상태에서 SCL/SDA가 모두 HIGH여야 한다.
- SDA가 LOW로 stuck되면 slave가 byte 전송 중이라고 생각하는 상태일 수 있고, SCL clock을 추가로 넣어 release시킬 수 있다.
- embedded debug에서는 HAL return value만 보지 말고 peripheral register와 GPIO line level까지 내려가야 원인을 확정할 수 있다.

## 11. 현재 코드의 주의점

현재 `imu_bno055.c`에는 디버그 로그가 많이 들어가 있다.

개발 중에는 유용하지만, 최종 제출 또는 안정화 펌웨어에서는 다음처럼 정리하는 것이 좋다.

- bus recovery 로직은 유지
- 상세 진단 로그는 `#define DEBUG_BNO055_INIT 1` 같은 매크로로 감싸기
- 정상 부팅 시에는 최소 로그만 출력
- `HAL_Delay(1)` 기반 recovery는 boot 단계에서만 실행되므로 실시간 제어 루프에는 영향 없음

권장 최종 정책:

```text
if I2C BUSY at boot:
    run bus recovery
    retry BNO055 init
else:
    normal BNO055 init
```

## 12. 결과

BNO055 초기화 실패 문제는 해결되었다.

최종 확인 로그:

```text
[BNO055] IsDeviceReady 0x28 -> HAL_OK (0), ErrorCode=0x00000000
[BNO055] addr=0x28 ready
[BNO055] CHIP_ID OK
[BOOT] BNO055 init OK

=== Control loop started (50Hz, UART) ===
```

이후 control loop가 50 Hz로 정상 시작했고, status/fault 로그도 정상적으로 출력되었다.
