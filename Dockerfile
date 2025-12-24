FROM python:3.11-slim

LABEL maintainer="Intellichem2MQTT Contributors"
LABEL description="Pentair IntelliChem to MQTT Bridge for Home Assistant"
LABEL version="1.0.0"

# Set working directory
WORKDIR /app

# Install system dependencies for serial communication
RUN apt-get update && apt-get install -y --no-install-recommends \
    && rm -rf /var/lib/apt/lists/*

# Copy requirements first for better caching
COPY requirements.txt .

# Install Python dependencies
RUN pip install --no-cache-dir -r requirements.txt

# Copy application code
COPY intellichem2mqtt/ ./intellichem2mqtt/
COPY pyproject.toml .

# Install the application
RUN pip install --no-cache-dir -e .

# Create config directory
RUN mkdir -p /config

# Default config location
ENV INTELLICHEM_CONFIG=/config/config.yaml

# Run as non-root user for security
RUN useradd -r -s /bin/false intellichem && \
    chown -R intellichem:intellichem /app /config

# Add user to dialout group for serial port access
RUN usermod -a -G dialout intellichem

USER intellichem

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD python -c "import sys; sys.exit(0)"

# Run the application
ENTRYPOINT ["python", "-m", "intellichem2mqtt"]
CMD ["--config", "/config/config.yaml"]
