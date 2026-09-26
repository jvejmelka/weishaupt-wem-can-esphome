FROM node:20-alpine
WORKDIR /app
COPY package*.json ./
RUN npm install --omit=dev
COPY public ./public
COPY src ./src
# Versionsnummer (Timestamp) in Service Worker und index.html einbauen
RUN VERSION=$(date +%s) && \
    sed -i "s/__VERSION__/$VERSION/g" /app/public/sw.js && \
    sed -i "s/__VERSION__/$VERSION/g" /app/public/index.html
ENV PORT=3000
EXPOSE 3000
CMD ["npm", "start"]
