# Use a C++17 compatible base image (GCC 13)
FROM gcc:13

# Set the working directory
WORKDIR /app

# Copy source code and Makefile
# We copy these first to leverage Docker's build cache.
COPY Makefile *.cpp ./

# Create the file directories that the server/client expect
RUN mkdir -p server_files client_files

# (Optional) Add dummy files so the server isn't empty on start
RUN echo "hello from server inside docker" > server_files/test1.txt
RUN echo "hello from client dir" > client_files/upload_me.txt

# Compile the project using the Makefile
# This will create the build/server and build/client binaries
RUN make

# Expose the server port to the host
EXPOSE 9999

# Set the default command to run the server when the container starts
CMD ["./build/server"]