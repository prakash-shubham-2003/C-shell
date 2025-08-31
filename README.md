# Shell Design

## Description
This project is designed to provide a unique command-line interface with additional features and functionalities.

## Features
- Custom command execution
- Locking mechanism
- Utility functions
- Malware detection

## Installation
To install and run the custom shell, follow these steps:

1. Clone the repository:
    ```sh
    git clone <repository_url>
    ```
3. Compile the project using the provided makefile:
    ```sh
    make
    ```
4. Run the custom shell:
    ```sh
    ./terminal
    ```


## Files Description
- `heuristic.txt`: Contains heuristic data for malware detection.
- `lock.cpp`: Implements the locking mechanism for the shell.
- `main.cpp`: The main entry point of the custom shell.
- `makefile`: Build script to compile the project.
- `malware.c`: Contains functions for malware detection.
- `README.md`: Project documentation.
- `test.cpp`: Contains test cases for the custom shell.
- `utility.cpp`: Implements various utility functions.
- `utility.hpp`: Header file for utility functions.
