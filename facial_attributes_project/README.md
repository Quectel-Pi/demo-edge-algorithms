# Facial Attributes Demo

Qt5 desktop demo for Quectel Pi M2, RK3576, Debian aarch64. Uses the supplied RK3576 Linux facial attributes SDK without changing its authorization behavior.

Features: USB camera and local images, multiple faces, seven emotion scores, estimated age, gender scores, confidence/NMS controls, annotated JPG and JSON export, inference timing and device status. The three-column layout follows `face_project`. Face numbers are frame-local, not tracking identities. Predictions are model estimates, not verified personal attributes or emotional states.

See [Chinese setup and usage](README.zh.md) for quick-start instructions.

```sh
cd /home/pi/facial_attributes_project
sh setup_from_sdk.sh /home/pi/facial_attributes_detection_RK3576
sh build.sh
sh run.sh
```

Replace `REPLACE_WITH_ALGORITHM_AUTH_CODE` in `run.sh` with the SDK test code or your integrator-issued algorithm code. `FAD_AUTH_CODE` can also be set in the environment to override the placeholder. Do not share real codes in chat or public repositories; restore the placeholder before sharing the project. Empty codes are rejected by the SDK; offline trial is solely decided by the SDK. `FAD_LICENSE_FILE` defaults to the absolute writable path `license/fad_license.txt`. SDK module is 3, algorithm appid is SDK-managed 147.

```sh
sh run.sh --camera auto
sh run.sh --image images/input.jpg
```

Use **Save Result** to export the current annotated image and its JSON results to `outputs/`. The JSON includes face boxes, detection scores, seven emotion scores, gender scores, estimated age, image dimensions and inference time. `F` toggles fullscreen and `Esc` exits.

The SDK does not provide landmarks, face recognition, liveness or mask detection on this platform. These are not simulated. Use only bundled OpenCV world 4.2.0 to avoid ABI conflicts.

