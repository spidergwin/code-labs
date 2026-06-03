import time


def countdown():
    for i in range(3, 0, -1):
        print(i)
        time.sleep(0.5)
