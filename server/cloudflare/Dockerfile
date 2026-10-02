# Build context is the repository root (see wrangler.jsonc).
FROM python:3.12-slim
WORKDIR /app
COPY linux /app/linux
COPY server/mute_server.py /app/
RUN pip install --no-cache-dir /app/linux
ENV PYTHONUNBUFFERED=1 MUTE_PORT=8080
EXPOSE 8080
CMD ["python", "/app/mute_server.py"]
