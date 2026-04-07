// One-time setup script — run this once to create both Stripe products.
// Usage:
//   STRIPE_SECRET_KEY=sk_live_... node create-stripe-product.js
//
// Creates:
//   Due Light device   — $20.00  → STRIPE_PRICE_ID
//   USB-C cable add-on — $2.00   → STRIPE_CABLE_PRICE_ID
//
// After running, copy both printed values into your .env file.

require('dotenv').config();
const Stripe = require('stripe');

const secretKey = process.env.STRIPE_SECRET_KEY;
if (!secretKey) {
  console.error('Error: STRIPE_SECRET_KEY is not set.');
  process.exit(1);
}
const stripe = Stripe(secretKey);

async function main() {
  console.log('Creating Due Light products in Stripe...\n');

  const device = await stripe.products.create({
    name: 'Due Light',
    description: 'LED assignment deadline tracker. Works with any school that uses Canvas. One-time purchase, no subscription.',
    default_price_data: { currency: 'usd', unit_amount: 2000 },
    shippable: true,
  });

  const cable = await stripe.products.create({
    name: 'USB-C Cable',
    description: 'USB-C power cable for Due Light.',
    default_price_data: { currency: 'usd', unit_amount: 200 },
    shippable: true,
  });

  console.log('Done!\n');
  console.log('Add these to your .env file:');
  console.log(`STRIPE_PRICE_ID=${device.default_price}`);
  console.log(`STRIPE_CABLE_PRICE_ID=${cable.default_price}`);
}

main().catch((err) => {
  console.error('Stripe error:', err.message);
  process.exit(1);
});
