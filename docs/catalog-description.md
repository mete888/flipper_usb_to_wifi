# Internet Bridge

Use a computer's existing internet connection from Flipper Zero through an explicitly authorized USB or Bluetooth connection. Find the app in Tools, then select the connection mode. The Flipper does not join Wi-Fi, receive the Wi-Fi password, or appear as a USB network adapter.

The Flipper app sends bounded requests over USB CDC or Bluetooth GATT. A desktop host validates them, performs HTTPS requests, and streams responses back in small chunks. A native macOS menu bar app, optional Windows/Linux graphical helper and Windows/Linux/macOS command-line host are included.

## Features

- Test the selected bridge connection
- Search Binance Spot USDT coins and view gold, silver, and Binance Futures
  Brent prices in one auto-refreshing Markets menu
- Fetch sample text and the current date and time
- Search English Wikipedia
- Check weather by location
- Read the live National Today paragraph with inline bold names and scrolling
- View the current ISS position
- Find internet radio stations by country and play audio on the Flipper speaker (USB only)
- Send a custom HTTPS GET request
- Read the latest ten USGS earthquake events
- Convert amounts between 21 currencies using dated reference rates
- Look up English word definitions and examples

## Requirements

- Flipper Zero with a microSD card
- macOS 13 or later, Windows 10/11, or a current Linux distribution
- A companion desktop host from the [project repository](https://github.com/mete888/flipper_internet_bridge)
- A data-capable USB cable for USB mode, or compatible Bluetooth support for wireless mode

The helper does not require administrator access. USB access requires **Allow Once** or **Always Allow**. Bluetooth requires selecting a bridge computer, code-based recognition for new peers and a separate **Allow Once** / **Deny** decision for each connection. Pairing alone does not grant internet access.

## Security

Only HTTPS is allowed. Localhost, private networks, link-local addresses, unsafe redirects, shared cookies, stored credentials, and non-HTTPS schemes are blocked. USB disconnection cancels active network work immediately.
