#!/usr/bin/env python3
"""Regression checks for slow boot polls and cleanup, without Android or ADB."""
import copy
import subprocess
import unittest
from unittest import mock

import solar_performance_android as runner


class RunnerTimeoutChecks(unittest.TestCase):
    def test_slow_boot_poll_retries_without_extending_budget(self):
        for budget,boot_expected in [(12,True),(3,False)]:
            with self.subTest(budget=budget):
                clock=[0.0];timeouts=[];report={};saved=[]
                emulator=mock.Mock();emulator.poll.return_value=None
                def run(args,**kwargs):
                    timeouts.append(kwargs['timeout'])
                    if len(timeouts)==1:
                        clock[0]+=kwargs['timeout']
                        raise subprocess.TimeoutExpired(args,kwargs['timeout'])
                    clock[0]+=1
                    return subprocess.CompletedProcess(args,0,stdout='1\n')
                def sleep(seconds):clock[0]+=seconds
                with mock.patch.object(runner.subprocess,'run',side_effect=run),mock.patch.object(runner.time,'monotonic',side_effect=lambda:clock[0]),mock.patch.object(runner.time,'sleep',side_effect=sleep):
                    if boot_expected:
                        runner.wait_for_boot(emulator,'adb','emulator-test',{},budget,report,lambda:saved.append(copy.deepcopy(report)))
                    else:
                        with self.assertRaisesRegex(TimeoutError,'boot exceeded budget'):
                            runner.wait_for_boot(emulator,'adb','emulator-test',{},budget,report,lambda:saved.append(copy.deepcopy(report)))
                self.assertEqual(report['boot_poll_timeouts'],1)
                self.assertTrue(saved)
                self.assertLessEqual(report['boot_seconds'],budget)
                self.assertEqual(len(timeouts),2 if boot_expected else 1)
                self.assertEqual(timeouts[0],min(5,budget))

    def test_slow_cleanup_keeps_primary_failure_and_saves_report(self):
        for initial_status in ['blocked_or_failed','passed']:
            with self.subTest(initial_status=initial_status):
                report={'status':initial_status,'stage':'running_instrumentation'}
                if initial_status!='passed':report['error']='Original instrumentation failure'
                saved=[]
                with mock.patch.object(runner.subprocess,'run',side_effect=subprocess.TimeoutExpired('adb kill-server',10)):
                    runner.cleanup(None,'adb',{},report,lambda:saved.append(copy.deepcopy(report)))
                self.assertEqual(report['status'],'blocked_or_failed')
                self.assertEqual(report['stage'],'finished')
                self.assertEqual(report['stage_before_cleanup'],'running_instrumentation')
                self.assertIn('TimeoutExpired',report['cleanup_errors'][0])
                self.assertEqual(saved[-1],report)
                if initial_status!='passed':self.assertEqual(report['error'],'Original instrumentation failure')


if __name__=='__main__':unittest.main()
