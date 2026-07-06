/**
 * =============================================================================
 *  Vision Hub — AMB82-MINI | Distributed Robotic Car Platform
 * =============================================================================
 *  Role       : Isolated Vision Hub (no motor / actuator logic)
 *  SoC        : Realtek RTL8735B (Ameba Pro 2)
 *  SDK        : Realtek Ameba Arduino SDK v4.1.0
 *
 *  Pipeline Overview:
 *
 *    ┌────────────────────────────────────────────────────────────────────┐
 *    │  ISP / Camera Hardware                                             │
 *    │                                                                    │
 *    │  Channel 0 (H.264 VGA 640×480 @ 20fps)                            │
 *    │      ──► videoStreamer (StreamIO) ──► RTSP Server                  │
 *    │              ▲                                                     │
 *    │          OSD overlay drawn before H.264 compression                │
 *    │                                                                    │
 *    │  Channel 2 (RGB 416×416 @ 20fps — raw, for NPU)                   │
 *    │      ──► videoStreamerNN (StreamIO) ──► NNObjectDetection          │
 *    │                                              │                    │
 *    │                                     onDetectionResult()           │
 *    └────────────────────────────────────────────────────────────────────┘
 *
 *  Network  : Wi-Fi SoftAP — SSID "RobotCar-Demo", Channel 1
 *  Model    : YOLOv3-Tiny (DEFAULT_YOLOV3TINY), INT8-quantised
 * =============================================================================
 */

#include "WiFi.h"
#include "StreamIO.h"
#include "VideoStream.h"
#include "RTSP.h"
#include "NNObjectDetection.h"
#include "VideoStreamOverlay.h"
#include "ObjectClassList.h"    // COCO 80-class lookup table

// ---------------------------------------------------------------------------
// Hardware channel assignments
//   V1_CHANNEL (0) = H.264 encoder output → RTSP + OSD
//   Camera index 3  = raw RGB ISP output  → NPU inference
//   NOTE: ISP channel map: 0=HEVC/H264, 1=JPEG, 2=NV12, 4=RGB-only
//   The RGB-only output sits at camera *index* 3 (maps to hw stream ID 4).
//   This is what the official SDK examples call CHANNELNN=3.
// ---------------------------------------------------------------------------
#define CHANNEL_AUDIENCE  0    // H.264 stream for viewers
#define CHANNEL_NN        3    // Raw RGB feed for YOLOv3-Tiny NPU (NOT 2!)

// ---------------------------------------------------------------------------
// NN inference input resolution — must match YOLOv3-Tiny's expected input
// ---------------------------------------------------------------------------
#define NN_WIDTH  416
#define NN_HEIGHT 416

// ---------------------------------------------------------------------------
// SoftAP credentials — Channel 1 avoids ESP-NOW interference
// ---------------------------------------------------------------------------
static const char AP_SSID[]     = "RobotCar-Demo";
static const char AP_PASSWORD[] = "openday2025";
static const char AP_CHANNEL[]  = "1";   // apbegin() takes channel as char*

// ---------------------------------------------------------------------------
// Module instances
// ---------------------------------------------------------------------------

// Channel 0: VGA H.264 @ 20fps
// GOP=10 → keyframe every 0.5 s at 20fps → low RTSP latency
// CBR mode (_rc_mode=1) → consistent frame sizes → less jitter
VideoSetting configAudience(VIDEO_VGA, 20, VIDEO_H264, 0);

// Channel 3: 416×416 raw RGB @ 20fps — unencoded feed directly to NPU
VideoSetting configNN(NN_WIDTH, NN_HEIGHT, 20, VIDEO_RGB, 0);

NNObjectDetection ObjDet;               // NPU object detection engine
RTSP              rtsp;                 // RTSP server (Ch 0)

// Two independent DMA-backed hardware pipelines
StreamIO videoStreamer(1, 1);           // Camera Ch0 → RTSP
StreamIO videoStreamerNN(1, 1);         // Camera Ch2 → NPU

// Runtime state — populated after SoftAP comes up
static IPAddress apIP;
static int       rtspPort = 0;

// ---------------------------------------------------------------------------
// Forward declaration of the async inference callback
// ---------------------------------------------------------------------------
void onDetectionResult(std::vector<ObjectDetectionResult> results);

// ===========================================================================
//  setup()
// ===========================================================================
void setup()
{
    Serial.begin(115200);
    Serial.println("\n[VisionHub] Booting...");

    // -----------------------------------------------------------------------
    // 1. Bring up SoftAP on Channel 1
    //    apbegin(ssid, password, channel) — channel is a char* string
    // -----------------------------------------------------------------------
    Serial.println("[VisionHub] Starting SoftAP on Channel 1...");
    WiFi.apbegin(
        const_cast<char*>(AP_SSID),
        const_cast<char*>(AP_PASSWORD),
        const_cast<char*>(AP_CHANNEL)
    );

    // SoftAP default gateway is 192.168.1.1 on this SDK
    apIP = WiFi.localIP();
    Serial.print("[VisionHub] SoftAP IP: ");
    Serial.println(apIP);
    if (apIP != IPAddress(192, 168, 1, 1)) {
        Serial.println("[VisionHub] WARNING: IP is not 192.168.1.1 — check AP config.");
    }

    // -----------------------------------------------------------------------
    // 2. Configure dual camera channels
    //    Audience stream:
    //      - 1 Mbps bitrate  (lower than 2Mbps → less buffer → less latency)
    //      - GOP = 10        (keyframe every 0.5 s → decoder can sync faster)
    //      - RC mode 2 (VBR) for best quality at the set bitrate cap
    // -----------------------------------------------------------------------
    configAudience.setBitrate(1 * 1024 * 1024);  // 1 Mbps — reduces buffering latency
    configAudience._gop     = 10;                 // Keyframe every 10 frames (~0.5 s at 20fps)
    configAudience._rc_mode = 2;                  // 2=VBR, 1=CBR
    Camera.configVideoChannel(CHANNEL_AUDIENCE, configAudience);
    Camera.configVideoChannel(CHANNEL_NN,       configNN);
    Camera.videoInit();
    Serial.println("[VisionHub] Camera initialised (Ch0=VGA/H264 GOP10 1Mbps, Ch3=416x416/RGB).");

    // -----------------------------------------------------------------------
    // 3. Initialise RTSP server (Ch 0 — audience stream)
    // -----------------------------------------------------------------------
    rtsp.configVideo(configAudience);
    rtsp.begin();
    rtspPort = rtsp.getPort();
    Serial.print("[VisionHub] RTSP ready → rtsp://");
    Serial.print(apIP);
    Serial.print(":");
    Serial.println(rtspPort);

    // -----------------------------------------------------------------------
    // 4. Initialise NPU object detection engine
    //    Model : DEFAULT_YOLOV7TINY — State-of-the-art tiny model, highly accurate.
    //            especially for person detection and multi-object scenes.
    //    configThreshold(confidence, nms):
    //      confidence = 0.55  → strict threshold to eliminate false positives (e.g. wall shadows)
    //      nms        = 0.30  → suppress overlapping duplicate boxes
    // -----------------------------------------------------------------------
    ObjDet.configVideo(configNN);
    ObjDet.modelSelect(OBJECT_DETECTION, DEFAULT_YOLOV7TINY, NA_MODEL, NA_MODEL);
    ObjDet.configThreshold(0.55, 0.30);   // confidence=0.55, nms=0.30
    ObjDet.setResultCallback(onDetectionResult);
    ObjDet.begin();
    Serial.println("[VisionHub] NPU / YOLOv7-Tiny initialised (conf=0.55, nms=0.30).");

    // -----------------------------------------------------------------------
    // 5. StreamIO pipeline A: Camera Ch0 → RTSP
    // -----------------------------------------------------------------------
    videoStreamer.registerInput(Camera.getStream(CHANNEL_AUDIENCE));
    videoStreamer.registerOutput(rtsp);
    if (videoStreamer.begin() != 0) {
        Serial.println("[VisionHub] ERROR: videoStreamer (Ch0→RTSP) link failed.");
    }
    Camera.channelBegin(CHANNEL_AUDIENCE);

    // -----------------------------------------------------------------------
    // 6. StreamIO pipeline B: Camera Ch2 → NNObjectDetection
    //    setStackSize / setTaskPriority — allocate enough stack for the NN thread
    // -----------------------------------------------------------------------
    videoStreamerNN.registerInput(Camera.getStream(CHANNEL_NN));
    videoStreamerNN.setStackSize();
    videoStreamerNN.setTaskPriority();
    videoStreamerNN.registerOutput(ObjDet);
    if (videoStreamerNN.begin() != 0) {
        Serial.println("[VisionHub] ERROR: videoStreamerNN (Ch2→NPU) link failed.");
    }
    Camera.channelBegin(CHANNEL_NN);

    // -----------------------------------------------------------------------
    // 7. OSD overlay — bound to Channel 0 (audience stream)
    //    Bounding boxes are composited into the ISP layer BEFORE H.264 encoding
    // -----------------------------------------------------------------------
    OSD.configVideo(CHANNEL_AUDIENCE, configAudience);
    OSD.begin();
    Serial.println("[VisionHub] OSD overlay initialised on Ch0.");

    Serial.println("[VisionHub] Setup complete — video + inference pipelines running.");
}

// ===========================================================================
//  loop()
//  All real work is handled by DMA channels, hardware interrupts, and the
//  NPU callback. Nothing to do here.
// ===========================================================================
void loop()
{
    delay(1000);    // Non-blocking idle; frees RTOS scheduler for media tasks
}

// ===========================================================================
//  onDetectionResult()
//  Async NPU callback — executed by the StreamIO NN task context whenever
//  a new inference frame is ready. Draws bounding boxes + labels onto the
//  hardware OSD layer of Ch 0.
//
//  Parameters:
//    results — vector of ObjectDetectionResult objects (normalised [0.0, 1.0])
// ===========================================================================
void onDetectionResult(std::vector<ObjectDetectionResult> results)
{
    // Audience stream pixel dimensions — used to scale normalised coords
    const uint16_t im_w = configAudience.width();
    const uint16_t im_h = configAudience.height();

    // -- Clear previous OSD frame (createBitmap resets the canvas for Ch 0) --
    OSD.createBitmap(CHANNEL_AUDIENCE);

    // If no objects detected this frame, flush the cleared canvas and return
    if (results.empty()) {
        OSD.update(CHANNEL_AUDIENCE);
        return;
    }

    // -- Iterate detections and render bounding boxes + labels ---------------
    for (size_t i = 0; i < results.size(); i++) {
        // SDK note: ObjectDetectionResult methods are not const-qualified;
        // take a non-const copy (matches the official SDK example pattern)
        ObjectDetectionResult item = results[i];

        int obj_type = item.type();

        // Bounds-check the class index against the 80-class COCO table
        if (obj_type < 0 || obj_type >= 80) {
            continue;
        }

        // Skip classes explicitly filtered out in ObjectClassList.h
        if (!itemList[obj_type].filter) {
            continue;
        }

        // -- Scale normalised [0.0 – 1.0] coordinates to pixel space ---------
        int xmin = static_cast<int>(item.xMin() * im_w);
        int ymin = static_cast<int>(item.yMin() * im_h);
        int xmax = static_cast<int>(item.xMax() * im_w);
        int ymax = static_cast<int>(item.yMax() * im_h);

        // -- Clamp coordinates to frame boundaries to prevent OSD overflow ---
        // NNObjectDetection.h does #undef min / #undef max, so use std::
        xmin = std::max(0, std::min(xmin, (int)im_w - 1));
        ymin = std::max(0, std::min(ymin, (int)im_h - 1));
        xmax = std::max(0, std::min(xmax, (int)im_w - 1));
        ymax = std::max(0, std::min(ymax, (int)im_h - 1));

        // Sanity check: degenerate box (zero area) → skip
        if (xmax <= xmin || ymax <= ymin) {
            continue;
        }

        // -- Draw green bounding box (line thickness = 3 px) -----------------
        OSD.drawRect(CHANNEL_AUDIENCE,
                     xmin, ymin, xmax, ymax,
                     3,                  // line_width
                     OSD_COLOR_GREEN,    // defined in VideoStreamOverlay.h
                     OSDLAYER0);

        // -- Build label: "ClassName  confidence%" ---------------------------
        char label[32];
        snprintf(label, sizeof(label), "%s %d%%",
                 itemList[obj_type].objectName,
                 item.score());

        // Place text immediately above the bounding box.
        // If there is insufficient vertical space, clamp to y=0.
        int text_h  = OSD.getTextHeight(CHANNEL_AUDIENCE);
        int text_y  = ymin - text_h;
        if (text_y < 0) {
            text_y = 0;
        }
        int text_x = std::max(0, xmin);

        OSD.drawText(CHANNEL_AUDIENCE,
                     text_x, text_y,
                     label,
                     OSD_COLOR_WHITE,    // defined in VideoStreamOverlay.h
                     OSDLAYER0);
    }

    // -- Push the completed frame buffer to the physical OSD hardware layer --
    OSD.update(CHANNEL_AUDIENCE);
}
