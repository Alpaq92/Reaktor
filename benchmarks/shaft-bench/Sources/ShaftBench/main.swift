import Foundation
import Shaft
import ShaftSetup

let arguments = CommandLine.arguments

func flag(_ name: String, _ fallback: Double) -> Double {
    guard let i = arguments.firstIndex(of: name), i + 1 < arguments.count,
          let value = Double(arguments[i + 1])
    else { return fallback }
    return value
}

let benchSeconds = flag("--bench-seconds", 5)
let benchFps = flag("--fps", 0)
let benchBoxes = Int(flag("--boxes", 64))

var painted = Size(0, 0)

ShaftSetup.useDefault()
runApp(BenchApp())

final class BenchApp: StatefulWidget {
    func createState() -> BenchState { BenchState() }
}

final class BenchState: State<BenchApp> {
    private let time = ValueNotifier<Double>(0.0)

    private var start: Date?
    private var frames = 0
    private var due = 0.0
    private var finished = false
    private var timer: Shaft.Timer?

    override func initState() {
        super.initState()

        SchedulerBinding.shared.addPostFrameCallback { [self] _ in
            if let view = View.maybeOf(context) as? DesktopView {
                view.size = Size(800, 600)
            }
            scheduleNext()
        }
    }

    private func scheduleNext() {
        SchedulerBinding.shared.addPostFrameCallback { [self] _ in onFrame() }
        SchedulerBinding.shared.scheduleFrame()
    }

    private func onFrame() {
        if finished { return }

        if start == nil { start = Date() }
        let t = Date().timeIntervalSince(start!)

        frames += 1
        time.value = t

        if t >= benchSeconds {
            finish(t)
            return
        }

        if benchFps > 0 {
            due = max(due + 1.0 / benchFps, t)
            let delay = due - t

            if delay > 0 {
                timer = backend.createTimer(
                    .microseconds(Int(delay * 1_000_000)),
                    repeat: false
                ) { [self] in scheduleNext() }
                return
            }
        }
        scheduleNext()
    }

    private func finish(_ elapsed: Double) {
        finished = true

        let size = painted
        print("size          \(Int(size.width))x\(Int(size.height))")
        print("frames        \(frames)")
        print("fps           \(String(format: "%.1f", Double(frames) / elapsed))")
        fflush(stdout)

        backend.stop()
        exit(0)
    }

    override func build(context: BuildContext) -> Widget {
        CustomPaint(painter: BoxPainter(time: time)) {
            SizedBox.expand()
        }
    }
}

final class BoxPainter: CustomPainterBase {
    let time: ValueNotifier<Double>

    init(time: ValueNotifier<Double>) {
        self.time = time
        super.init(repaint: time)
    }

    override func paint(canvas: Canvas, size: Size) {
        painted = size

        let w = Int(size.width)
        let h = Int(size.height)
        let stepX = w / 8, halfX = w / 16
        let stepY = h / 8, halfY = h / 16
        let r = Double(h / 24)
        let t = time.value

        var background = Paint()
        background.color = .rgb(247, 247, 247)
        canvas.drawRect(
            Rect(left: 0, top: 0, width: size.width, height: size.height),
            background
        )

        for i in 0..<benchBoxes {
            let a = t * (1.0 + 0.05 * Double(i))
            let cx = Double((i % 8) * stepX + halfX)
            let cy = Double((i / 8) * stepY + halfY)
            let path = backend.renderer.createPath()

            for k in 0..<4 {
                let angle = a + Double(k) * Double.pi / 2
                let x = Float(cx + r * cos(angle))
                let y = Float(cy + r * sin(angle))

                if k == 0 { path.moveTo(x, y) } else { path.lineTo(x, y) }
            }

            var paint = Paint()
            paint.color = .rgb(UInt8(60 + (i * 3) % 190), 140, 220)
            canvas.drawPath(path, paint)
        }
    }

    override func shouldRepaint(_ oldDelegate: CustomPainter) -> Bool { false }
}
