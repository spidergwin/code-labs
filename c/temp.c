#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/**
 * Celsius To Fahrenheit converter
 *
 * This is a program that accepts temperature in celsius and converts to
 * fahrenheit.
 */

/**
 * Algorithm for the program.
 *
 * 1) Declare the following variables.
 *     int choice
 *     float input
 *     float result
 * 2) Ask user for choice of conversion to make.
 *      Fahrenheit to Celsius.
 *      Celsius to Fahrenheit.
 * 3) Based on the user choice. Ask for either:
 *      Temperature in Celsius.
 *      Temperature in Fahrenheit.
 * 4) Make the conversion, store result in &result variable.
 * 5) Output the result to the user with appropriate value.
 */

void program();
void conv_cel_to_fah();
void conv_fah_to_cel();

int choice;     /* user choice for type of conversion to make */
char line[100]; /* input buffer for fgets */
int cont = 1;

int main() {
  while (cont) {
    program();
  }
  return 0;
}

void program() {
  char userInput[4] = "";

  printf("What type of conversion would you like to make?: \n");
  printf("1) Celsius to fahrenheit.\n");
  printf("2) Fahrenheit to Celsius.\n");

  while (1) {
    printf("(1 or 2) => ");
    fgets(line, sizeof(line), stdin);
    sscanf(line, "%d", &choice);

    if (choice == 1 || choice == 2) {
      break;
    }
  }
  switch (choice) {
  case 1:
    conv_cel_to_fah();
    break;
  case 2:
    conv_fah_to_cel();
    break;
  default:
    printf("Something unexpected went wrong!\n");
  }

  printf("Would you like to continue (Y or n): ");
  fgets(userInput, sizeof(userInput), stdin);
  userInput[strcspn(userInput, "\n")] = '\0';

  for (int i = 0; userInput[i]; i++) {
    userInput[i] = tolower((unsigned char)userInput[i]);
  }

  bool quit = strcmp(userInput, "n") == 0 || strcmp(userInput, "no") == 0;
  printf("%d\n", quit);

  if (quit) {
    cont = 0;
  }
}

void conv_cel_to_fah() {
  float input = 0.0f;
  float result = 0.0f;

  printf("Enter temperature in Celsius: ");
  fgets(line, sizeof(line), stdin);
  sscanf(line, "%f", &input);

  result = (input * 1.8) + 32;

  printf("%.2fC in Fahrenheit = %.2fF\n", input, result);
}

void conv_fah_to_cel() {
  float input = 0.0f;
  float result = 0.0f;

  printf("Enter temperature in Fahrenheit: ");
  fgets(line, sizeof(line), stdin);
  sscanf(line, "%f", &input);

  result = (input - 32) * 5 / 9;

  printf("%.2fF in Celsius = %.2fC\n", input, result);
}
