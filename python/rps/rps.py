import sys
import random
from functions import countdown
from enum import Enum


class Choice(Enum):
    ROCK = 1
    PAPER = 2
    SCISSORS = 3


print("RPS".center(30, " "))

print("Enter your choice: \n1) ROCK\n2) PAPER\n3) SCISSORS\n")
countdown()

playerchoice = input("(1, 2, 3) ==> ")
player = int(playerchoice)

if not isinstance(player, int) or player < 1 or player > 3:
    sys.exit("Please select either 1, 2 or 3 as input!")

computerchoice = random.choice("123")
computer = int(computerchoice)

print("")
print(f"You chose {Choice(player).name}")
print(f"Python chose {Choice(computer).name}")
print("")

if player == 1 and computer == 3:
    print("Hurray! You win")
elif player == 2 and computer == 1:
    print("Hurray! You win")
elif player == 3 and computer == 2:
    print("Hurray! You win")
elif player == computer:
    print("Tie game!")
else:
    print("Python Wins!")
