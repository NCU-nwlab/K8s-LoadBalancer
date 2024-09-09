FROM node
WORKDIR /usr/src/app
COPY package.json ./
RUN npm install
COPY server.js /usr/src/app
EXPOSE 3000
CMD ["node", "server.js"]
