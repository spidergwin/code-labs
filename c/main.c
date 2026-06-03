#include <stdio.h>

int main() {

  printf("*** Program to Calculate the total and average of A Set of Numbers "
         "***\n");
  char input[100];
  int Max_Number = 5;
  int nums[Max_Number];
  float total;
  float average;

  printf("Enter Number 1 - 5: ");
  fgets(input, sizeof(input), stdin);
  sscanf(input, "%d %*[^0-9] %d %*[^0-9] %d %*[^0-9] %d %*[^0-9] %d", &nums[0],
         &nums[1], &nums[2], &nums[3], &nums[4]);

  for (int i = 0; i < Max_Number; i++) {
    total += nums[i];
  }

  printf("The Total is %.2f\n", total);
  average = total / Max_Number;

  printf("The average of all scores is %.2f \n", average);

  return 0;
}
