// The rotating boxes of samples/bench/bench.c, drawn by Compose Multiplatform.
//
// Compose on the desktop is Skia through Skiko inside a Swing window. Two
// things about that window had to be found out by running it rather than by
// reading about it:
//
// A WindowState size is the frame, and asking for 800x600 that way draws into
// 784x561. So the size is declared by the content instead - the canvas asks
// for exactly 800x600 device pixels, converted through the current density so
// a scaled display does not quietly change the picture, and the window packs
// around it.
//
// And withFrameNanos is not a rate to sleep in. Skipping the vsyncs that are
// not due held 42.7 fps rather than 60, because the state written inside the
// callback is drawn a frame later and the loop re-registers after that. So the
// wait is an explicit delay, the way bench.c sleeps out the rest of its frame,
// and withFrameNanos is asked for one frame once the wait is over.
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application
import androidx.compose.ui.window.rememberWindowState
import java.util.concurrent.atomic.AtomicLong
import kotlinx.coroutines.delay
import kotlin.math.cos
import kotlin.math.sin
import kotlin.system.exitProcess

private const val W = 800
private const val H = 600
private val BACKGROUND = Color(0xFFF7F7F7)

// Written by the draw pass, read by the report. The drawing happens on the
// composition's thread and the report is posted from the frame loop, so the
// counters are atomic rather than plain.
private val drawNanos = AtomicLong()
private val paintedWidth = AtomicLong()
private val paintedHeight = AtomicLong()

private fun flag(args: Array<String>, name: String, fallback: Double): Double {
    val i = args.indexOf(name)
    if (i < 0 || i + 1 >= args.size) return fallback
    return args[i + 1].toDoubleOrNull() ?: fallback
}

fun main(args: Array<String>) {
    val seconds = flag(args, "--bench-seconds", 5.0)
    val fps = flag(args, "--fps", 0.0)
    val boxes = flag(args, "--boxes", 64.0).toInt()

    application {
        Window(
            onCloseRequest = ::exitApplication,
            // Unspecified, so the window packs around what the content asks
            // for rather than deducting its frame from what it was told.
            state = rememberWindowState(size = DpSize.Unspecified),
            resizable = false,
            title = "Kotlin Multiplatform",
        ) {
            Bench(seconds, fps, boxes)
        }
    }
}

@Composable
private fun Bench(seconds: Double, fps: Double, boxes: Int) {
    var time by remember { mutableStateOf(0.0f) }

    LaunchedEffect(Unit) {
        val period = if (fps > 0) (1_000_000_000.0 / fps).toLong() else 0L
        var start = 0L
        var due = 0L
        var elapsedNanos = 0L
        var frames = 0

        while (true) {
            // Held to a rate, the wait is an explicit sleep rather than a
            // vsync block, so the run is charged for the drawing and not the
            // wait.
            if (period > 0 && due > 0) {
                val waitMs = (due - System.nanoTime()) / 1_000_000
                if (waitMs > 0) delay(waitMs)
            }

            val stop = withFrameNanos { now ->
                if (start == 0L) start = now

                due = if (due == 0L) System.nanoTime() + period
                      else maxOf(due + period, System.nanoTime())
                frames++
                elapsedNanos = now - start
                time = elapsedNanos / 1e9f
                elapsedNanos >= (seconds * 1e9).toLong()
            }

            if (stop) {
                val elapsed = elapsedNanos / 1e9
                val perFrame = drawNanos.get() / 1e6 / frames

                println("size          ${paintedWidth.get()}x${paintedHeight.get()}")
                println("frames        $frames")
                println("fps           %.1f".format(frames / elapsed))
                println("ms_per_frame  %.2f".format(perFrame))
                println("cpu_at_60fps  %.1f".format(100 * perFrame / (1000.0 / 60)))
                System.out.flush()
                exitProcess(0)
            }
        }
    }

    val density = LocalDensity.current

    Canvas(
        Modifier.size(
            with(density) { W.toDp() },
            with(density) { H.toDp() },
        )
    ) {
        val began = System.nanoTime()
        val t = time

        // The integer divisions are bench.c's, kept integer here for the same
        // reason: a truncated grid step is part of the picture.
        val w = size.width.toInt()
        val h = size.height.toInt()
        val stepX = w / 8
        val halfX = w / 16
        val stepY = h / 8
        val halfY = h / 16
        val r = (h / 24).toFloat()

        paintedWidth.set(w.toLong())
        paintedHeight.set(h.toLong())
        drawRect(BACKGROUND)

        for (i in 0 until boxes) {
            val a = t * (1f + 0.05f * i)
            val cx = ((i % 8) * stepX + halfX).toFloat()
            val cy = ((i / 8) * stepY + halfY).toFloat()
            val path = Path()

            for (k in 0 until 4) {
                val angle = a + k * (Math.PI.toFloat() / 2f)
                val x = cx + r * cos(angle)
                val y = cy + r * sin(angle)

                if (k == 0) path.moveTo(x, y) else path.lineTo(x, y)
            }
            path.close()
            drawPath(path, Color(60 + (i * 3) % 190, 140, 220))
        }

        drawNanos.addAndGet(System.nanoTime() - began)
    }
}
