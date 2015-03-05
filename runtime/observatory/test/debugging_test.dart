// Copyright (c) 2015, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import 'package:observatory/service_io.dart';
import 'package:unittest/unittest.dart';
import 'test_helper.dart';
import 'dart:async';

void helper(i) {
  print(i);
}

void testFunction() {
  int i = 0;
  while (true) {
    if (++i % 100000000 == 0) {
      helper(i);  // line 18
    }
  }
}

var tests = [

// Pause
(Isolate isolate) {
  Completer completer = new Completer();
  isolate.vm.events.stream.listen((ServiceEvent event) {
    if (event.eventType == 'IsolateInterrupted') {
      completer.complete();
    }
  });
  isolate.pause();
  return completer.future;
},

// Resume
(Isolate isolate) {
  return isolate.resume().then((_) {
      expect(isolate.pauseEvent == null, isTrue);
  });
},

// Add breakpoint
(Isolate isolate) {
  return isolate.rootLib.load().then((_) {
      // Set up a listener to wait for breakpoint events.
      Completer completer = new Completer();
      List events = [];
      var subscription;
      subscription = isolate.vm.events.stream.listen((ServiceEvent event) {
        if (event.eventType.startsWith('Breakpoint')) {
          events.add(event);
          if (events.length == 2) {
            expect(events[0].eventType, equals('BreakpointResolved'));
            expect(events[1].eventType, equals('BreakpointReached'));
            print('Breakpoint reached');
            subscription.cancel();
            completer.complete();
          }
        }
      });

      // Add the breakpoint.
      var script = isolate.rootLib.scripts[0];
      return isolate.addBreakpoint(script, 18).then((result) {
          expect(result is Breakpoint, isTrue);
          Breakpoint bpt = result;
          expect(bpt.type, equals('Breakpoint'));
          expect(bpt.script.id, equals(script.id));
          expect(bpt.tokenPos, equals(67));
          expect(isolate.breakpoints.length, equals(1));
          return completer.future;  // Wait for breakpoint events.
      });
    });
},

// Get the stack trace
(Isolate isolate) {
  return isolate.getStack().then((ServiceMap stack) {
      expect(stack.type, equals('Stack'));
      expect(stack['frames'].length, greaterThanOrEqualTo(1));
      expect(stack['frames'][0]['function'].name, equals('testFunction'));
  });
},

// Stepping
(Isolate isolate) {
  // Set up a listener to wait for breakpoint events.
  Completer completer = new Completer();
  List events = [];
  var subscription;
  subscription = isolate.vm.events.stream.listen((ServiceEvent event) {
    if (event.eventType.startsWith('BreakpointReached')) {
      print('Breakpoint reached');
      subscription.cancel();
      completer.complete();
    }
  });
  
  return isolate.stepInto().then((isolate) {
    return completer.future;  // Wait for breakpoint events.
  });
},

// Get the stack trace again.  We are in 'helper'.
(Isolate isolate) {
  return isolate.getStack().then((ServiceMap stack) {
      expect(stack.type, equals('Stack'));
      expect(stack['frames'].length, greaterThanOrEqualTo(2));
      expect(stack['frames'][0]['function'].name, equals('helper'));
  });
},

// Remove breakpoint
(Isolate isolate) {
  expect(isolate.breakpoints.length, equals(1));
  var bpt = isolate.breakpoints[0];
  return isolate.removeBreakpoint(bpt).then((_) {
      expect(isolate.breakpoints.length, equals(0));
  });
},

// Resume
(Isolate isolate) {
  return isolate.resume().then((_) {
      expect(isolate.pauseEvent == null, isTrue);
  });
},

// Add breakpoint at function entry
(Isolate isolate) {
  // Set up a listener to wait for breakpoint events.
  Completer completer = new Completer();
  List events = [];
  var subscription;
  subscription = isolate.vm.events.stream.listen((ServiceEvent event) {
    if (event.eventType.startsWith('BreakpointReached')) {
      print('Breakpoint reached');
      subscription.cancel();
      completer.complete();
    }
  });
  
  // Find a specific function.
  ServiceFunction function = isolate.rootLib.functions.firstWhere(
      (f) => f.name == 'helper');
  expect(function, isNotNull);

  // Add the breakpoint at function entry
  return isolate.addBreakpointAtEntry(function).then((result) {
    expect(result is Breakpoint, isTrue);
    Breakpoint bpt = result;
    expect(bpt.type, equals('Breakpoint'));
    expect(bpt.script.name, equals('debugging_test.dart'));
    expect(bpt.tokenPos, equals(29));
    expect(isolate.breakpoints.length, equals(1));
    return completer.future;  // Wait for breakpoint events.
  });
},

];

main(args) => runIsolateTests(args, tests, testeeConcurrent: testFunction);