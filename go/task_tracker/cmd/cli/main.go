package main

import (
	"fmt"
	"os"
	"slices"
	"time"
)

/*
---Task Properties
Each task should have the following properties:

- id: A unique identifier for the task

- description: A short description of the task

- status: The status of the task (todo, in-progress, done)

- createdAt: The date and time when the task was created

- updatedAt: The date and time when the task was last updated

NOTE: Make sure to add these properties to the JSON file when adding a new task and update them when updating a task.
*/

type Status String

const (
	StatusInProgress Status = "in-progress"
	StatusTodo       Status = "todo"
	StatusDone       Status = "done"
)

type Task struct {
	Id          int64 `json:"id"`
	Description string `json:"description"`
	Status      Status `json:"status"`
	CreatedAt   time.Time `json:"created_at"`
	UpdatedAt   time.Time `json:"updated_at"`
}

func main() {
	fmt.Println("Welcome to the Task_Tracker CLI!")

	commands := []string{"add", "list", "update", "delete", "mark-done", "mark-in-progress"}

	args := os.Args[1:]

	if !slices.Contains(commands, args[0]) {
		fmt.Printf("Unknown command: %v\n", args[0])
		os.Exit(1)
	}

	var task Task = Task{Id: }

}
