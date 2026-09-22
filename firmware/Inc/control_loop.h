#ifndef CONTROL_LOOP_H
#define CONTROL_LOOP_H

/* 50Hz 메인 루프 진입 (반환하지 않음 — 무한 루프)
 * SPI decode → stale check → telemetry → safety → mode dispatch → SPI encode
 * Jetson과 50Hz로 full-duplex 통신하며 RL 명령 추종.
 */
void control_loop_run(void);

#endif /* CONTROL_LOOP_H */
