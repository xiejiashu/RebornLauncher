Overconfident assumption on Version.dat format -> inferred from launcher-side decoder path instead of generator behavior -> verify writer implementation first and enforce explicit plain/encrypted format contracts.
Encoding mojibake in Chinese comment -> introduced non-ASCII without verifying file encoding -> confirm encoding expectations before adding non-ASCII comments.
