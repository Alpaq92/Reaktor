import 'dart:async';
import 'dart:io';
import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/widgets.dart';

const Color _background = Color(0xFFF7F7F7);

late final double _seconds;
late final double _fps;
late final int _boxes;

Size _painted = Size.zero;

double _flag(List<String> args, String name, double fallback) {
  final int i = args.indexOf(name);
  if (i < 0 || i + 1 >= args.length) return fallback;
  return double.tryParse(args[i + 1]) ?? fallback;
}

void main(List<String> args) {
  _seconds = _flag(args, '--bench-seconds', 5);
  _fps = _flag(args, '--fps', 0);
  _boxes = _flag(args, '--boxes', 64).toInt();
  runApp(const _Bench());
}

class _Bench extends StatefulWidget {
  const _Bench();

  @override
  State<_Bench> createState() => _BenchState();
}

class _BenchState extends State<_Bench> {
  final ValueNotifier<double> _time = ValueNotifier<double>(0);
  final Stopwatch _clock = Stopwatch();

  int _frames = 0;
  double _buildMs = 0;
  double _rasterMs = 0;
  int _dueUs = 0;
  bool _done = false;

  @override
  void initState() {
    super.initState();
    SchedulerBinding.instance.addTimingsCallback(_timings);
    SchedulerBinding.instance.addPostFrameCallback((_) => _tick());
  }

  void _timings(List<FrameTiming> timings) {
    for (final FrameTiming f in timings) {
      _buildMs += f.buildDuration.inMicroseconds / 1000.0;
      _rasterMs += f.rasterDuration.inMicroseconds / 1000.0;
    }
  }

  void _tick() {
    if (_done) return;
    if (!_clock.isRunning) _clock.start();

    _time.value = _clock.elapsedMicroseconds / 1000000.0;
    SchedulerBinding.instance.addPostFrameCallback((_) => _afterFrame());
    SchedulerBinding.instance.scheduleFrame();
  }

  void _afterFrame() {
    if (_done) return;

    _frames++;
    final int now = _clock.elapsedMicroseconds;

    if (now >= _seconds * 1000000) {
      _finish(now / 1000000.0);
      return;
    }

    if (_fps > 0) {
      final int period = (1000000 / _fps).round();

      _dueUs = _dueUs + period > now ? _dueUs + period : now;
      final int waitUs = _dueUs - now;

      if (waitUs > 0) {
        Timer(Duration(microseconds: waitUs), _tick);
        return;
      }
    }
    _tick();
  }

  void _finish(double elapsed) {
    _done = true;

    Future<void>.delayed(const Duration(milliseconds: 300), () {
      final double perFrame = (_buildMs + _rasterMs) / _frames;

      stdout.write('size          ${_painted.width.round()}'
          'x${_painted.height.round()}\n');
      stdout.write('frames        $_frames\n');
      stdout.write('fps           ${(_frames / elapsed).toStringAsFixed(1)}\n');
      stdout.write('ms_build      ${(_buildMs / _frames).toStringAsFixed(2)}\n');
      stdout.write('ms_raster     ${(_rasterMs / _frames).toStringAsFixed(2)}\n');
      stdout.write('ms_per_frame  ${perFrame.toStringAsFixed(2)}\n');
      stdout.write('cpu_at_60fps  '
          '${(100 * perFrame / (1000 / 60)).toStringAsFixed(1)}\n');
      exit(0);
    });
  }

  @override
  void dispose() {
    _time.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return CustomPaint(
      painter: _BoxPainter(_time),
      child: const SizedBox.expand(),
    );
  }
}

class _BoxPainter extends CustomPainter {
  _BoxPainter(this.time) : super(repaint: time);

  final ValueListenable<double> time;

  @override
  void paint(Canvas canvas, Size size) {
    final double t = time.value;

    final int w = size.width.toInt();
    final int h = size.height.toInt();
    final int stepX = w ~/ 8;
    final int halfX = w ~/ 16;
    final int stepY = h ~/ 8;
    final int halfY = h ~/ 16;
    final int r = h ~/ 24;

    _painted = size;
    canvas.drawRect(Offset.zero & size, Paint()..color = _background);

    for (int i = 0; i < _boxes; i++) {
      final double a = t * (1 + 0.05 * i);
      final double cx = ((i % 8) * stepX + halfX).toDouble();
      final double cy = ((i ~/ 8) * stepY + halfY).toDouble();
      final Path path = Path();

      for (int k = 0; k < 4; k++) {
        final double ang = a + k * math.pi / 2;
        final double x = cx + r * math.cos(ang);
        final double y = cy + r * math.sin(ang);

        if (k == 0) path.moveTo(x, y);
        else        path.lineTo(x, y);
      }
      path.close();
      canvas.drawPath(
        path,
        Paint()..color = Color.fromARGB(255, 60 + (i * 3) % 190, 140, 220),
      );
    }
  }

  @override
  bool shouldRepaint(_BoxPainter oldDelegate) => false;
}
