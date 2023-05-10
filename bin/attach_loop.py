import argparse
import subprocess
import random
import time

usbip_path = "C:\\Program Files\\USBip\\usbip.exe"
random.seed()

def run(args):
        subprocess.run(args, stderr=subprocess.STDOUT, text=True)

def loop(usbip, remote, busid, max_timeout, count):
        for i in range(count):
                print(i)
                run([usbip, "attach", "--remote", remote, "--bus-id", busid])

                sec = random.randrange(max_timeout)
                time.sleep(sec)

                run([usbip, "detach", "--all"])

def parse_args():
        p = argparse.ArgumentParser(description='usbip attach/detach loop',
                                    formatter_class=argparse.ArgumentDefaultsHelpFormatter)

        p.add_argument('-r', '--remote', type=str, dest='remote', metavar='HOST', required=True,
                        help='usbip server address')

        p.add_argument('-b', '--bus-id', type=str, dest='busid', metavar='ID', required=True,
                        help='bus-id of USB device')

        p.add_argument('-t', '--max-timeout', type=int, default=4, dest='max_timeout', metavar='SEC',
                        help='max timeout before detach, seconds')

        p.add_argument('-p', '--program', type=str, default=usbip_path, dest='usbip', metavar='PATH',
                        help='path to usbip.exe')

        p.add_argument('count', type=int, default=0xFFFF, nargs='?', metavar='N', help='number of loops')

        return p.parse_args()

try:
        args = parse_args()
        loop(args.usbip, args.remote, args.busid, args.max_timeout, args.count)
except KeyboardInterrupt:
        pass
